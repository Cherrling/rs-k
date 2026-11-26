// SPDX-License-Identifier: GPL-2.0-only

//! Native object-pool lifecycle.
//!
//! Rust owns parameter validation, capacity calculation, per-CPU slot
//! initialization, partial-failure cleanup, reference accounting, forced
//! release, and final cleanup callback selection. C retains the packed ring
//! layouts, allocation primitives, CPU topology helpers, refcount atomics,
//! and the inline pop and push fast paths.

use crate::{
    bindings,
    ffi::{c_int, c_ulong, c_void},
};
use core::{mem, ptr};
use macros::export;

type Pool = bindings::objpool_head;
type Slot = bindings::objpool_slot;
type InitCallback = bindings::objpool_init_obj_cb;
type FiniCallback = bindings::objpool_fini_cb;

const INVALID: c_int = -(bindings::EINVAL as c_int);
const NO_MEMORY: c_int = -(bindings::ENOMEM as c_int);

/// Returns the first entry in the flexible array of a slot.
///
/// # Safety
///
/// `slot` must point to writable slot storage whose allocation includes room
/// for `pool.capacity` pointer entries.
#[inline]
unsafe fn entries(slot: *mut Slot) -> *mut *mut c_void {
    // SAFETY: The caller guarantees valid slot storage.
    unsafe { ptr::addr_of_mut!((*slot).entries).cast::<*mut c_void>() }
}

/// Returns the byte size of the fixed slot header.
///
#[inline]
fn slot_header_size() -> usize {
    // The bindgen representation has the same fixed header as C and gives
    // the flexible array zero size.
    mem::size_of::<Slot>()
}

/// Allocates and zeroes one per-CPU slot and its object storage.
///
/// # Safety
///
/// The returned pointer must be released with `rust_objpool_free_slot()`.
unsafe fn alloc_slot(size: usize, cpu: c_int, gfp: bindings::gfp_t) -> *mut Slot {
    // SAFETY: The caller provides the public objpool allocation context.
    let slot = unsafe { bindings::rust_objpool_alloc_slot(size, cpu, gfp) };
    if slot.is_null() {
        return ptr::null_mut();
    }

    // The C pool contract requires zeroed slot metadata and object storage.
    // SAFETY: The allocator returned a writable allocation of at least `size`.
    unsafe { ptr::write_bytes(slot.cast::<u8>(), 0, size) };
    slot.cast::<Slot>()
}

/// Initializes a slot and all objects assigned to it.
///
/// # Safety
///
/// `pool` must be a live pool under initialization. `slot`, `context`, and
/// `objinit` must satisfy the public objpool callback contract.
unsafe fn init_slot(
    pool: *mut Pool,
    slot: *mut Slot,
    nodes: usize,
    context: *mut c_void,
    objinit: InitCallback,
) -> c_int {
    // SAFETY: The caller guarantees a live pool and writable slot.
    let capacity = unsafe { (*pool).capacity } as u32;
    // SAFETY: The caller guarantees writable slot metadata.
    unsafe { (*slot).mask = capacity.wrapping_sub(1) };

    // SAFETY: The caller guarantees a live slot allocation.
    let slot_entries = unsafe { entries(slot) };
    // The object area starts after all capacity pointer entries.
    // SAFETY: The allocation includes this flexible-array extent.
    let mut object = unsafe { slot_entries.add(capacity as usize).cast::<c_void>() };
    // SAFETY: The caller guarantees a live pool.
    let object_size = unsafe { (*pool).obj_size } as usize;
    let mask = capacity.wrapping_sub(1);
    let mut tail = 0u32;

    for _ in 0..nodes {
        if let Some(objinit) = objinit {
            // SAFETY: The caller provides the public initialization callback
            // contract and a live object and context.
            let result = unsafe { objinit(object, context) };
            if result != 0 {
                return result;
            }
        }

        // SAFETY: `tail` is the next unpublished local sequence.
        unsafe { slot_entries.add((tail & mask) as usize).write(object) };
        // SAFETY: Every object except the last is followed by more object
        // storage in this allocation. The unused final pointer is never read.
        object = unsafe { object.cast::<u8>().add(object_size).cast::<c_void>() };
        tail = tail.wrapping_add(1);
        // SAFETY: The caller guarantees writable slot metadata.
        unsafe {
            (*slot).tail = tail;
            (*slot).last = tail;
            (*pool).nr_objs += 1;
        }
    }

    0
}

/// Frees every allocated slot and the slot-pointer array.
///
/// # Safety
///
/// `pool` must be a live pool whose slots are no longer reachable by users.
unsafe fn free_slots(pool: *mut Pool) {
    // SAFETY: The caller guarantees a live pool.
    let slots = unsafe { (*pool).cpu_slots };
    if slots.is_null() {
        return;
    }

    // SAFETY: This helper exposes the kernel CPU-count boundary.
    let nr_cpu_ids = unsafe { bindings::rust_objpool_nr_cpu_ids() };
    for cpu in 0..nr_cpu_ids {
        // SAFETY: The pointer array has `nr_cpu_ids` entries.
        let slot = unsafe { slots.add(cpu as usize).read() };
        if !slot.is_null() {
            // SAFETY: Slot storage came from `rust_objpool_alloc_slot()`.
            unsafe { bindings::rust_objpool_free_slot(slot.cast::<c_void>()) };
        }
    }

    // SAFETY: The slot-pointer array came from the CPU-slot allocator.
    unsafe {
        bindings::rust_objpool_free_slot(slots.cast::<c_void>());
        (*pool).cpu_slots = ptr::null_mut();
    }
}

/// Allocates and initializes all possible CPU slots.
///
/// # Safety
///
/// `pool` must be a live pool under initialization. `context` and `objinit`
/// must satisfy the public callback contract.
unsafe fn init_slots(
    pool: *mut Pool,
    nr_objs: usize,
    context: *mut c_void,
    objinit: InitCallback,
) -> c_int {
    // SAFETY: This helper exposes the kernel CPU topology boundary.
    let nr_cpu_ids = unsafe { bindings::rust_objpool_nr_cpu_ids() };
    // SAFETY: This helper exposes the kernel possible-CPU count.
    let nr_possible_cpus = unsafe { bindings::rust_objpool_num_possible_cpus() };
    if nr_cpu_ids < 0 || nr_possible_cpus <= 0 {
        // SAFETY: The CPU slot array is private to this initialization.
        unsafe { free_slots(pool) };
        return INVALID;
    }

    let mut cpu_count = 0usize;
    for cpu in 0..nr_cpu_ids {
        // SAFETY: This helper validates the kernel CPU topology query.
        if !unsafe { bindings::rust_objpool_cpu_possible(cpu) } {
            continue;
        }

        let possible = nr_possible_cpus as usize;
        let mut nodes = nr_objs / possible;
        if cpu_count < nr_objs % possible {
            nodes += 1;
        }
        cpu_count += 1;

        // SAFETY: The caller guarantees a live pool.
        let capacity = unsafe { (*pool).capacity } as usize;
        // SAFETY: The caller guarantees a live pool.
        let object_size = unsafe { (*pool).obj_size } as usize;
        let header_size = slot_header_size();
        let Some(size) = header_size
            .checked_add(capacity.saturating_mul(mem::size_of::<*mut c_void>()))
            .and_then(|size| size.checked_add(object_size.saturating_mul(nodes)))
        else {
            // SAFETY: No slot was published for this CPU.
            unsafe { free_slots(pool) };
            return NO_MEMORY;
        };

        // SAFETY: The caller guarantees a live pool.
        let gfp = unsafe { (*pool).gfp };
        // SAFETY: The allocation request has checked size and valid CPU,
        // GFP, and callback context.
        let slot = unsafe { alloc_slot(size, cpu, gfp) };
        if slot.is_null() {
            // SAFETY: Slots already published by this routine are private.
            unsafe { free_slots(pool) };
            return NO_MEMORY;
        }

        // SAFETY: The pointer array has `nr_cpu_ids` zeroed entries.
        unsafe { (*pool).cpu_slots.add(cpu as usize).write(slot) };
        // SAFETY: The slot is zeroed and has room for all assigned objects.
        let result = unsafe { init_slot(pool, slot, nodes, context, objinit) };
        if result != 0 {
            // SAFETY: The initialized slot and preceding slots are private.
            unsafe { free_slots(pool) };
            return result;
        }
    }

    0
}

/// Releases all slots and invokes the optional cleanup callback.
///
/// # Safety
///
/// `pool` must be a live initialized pool with no remaining users.
unsafe fn free_pool(pool: *mut Pool) {
    // SAFETY: The caller guarantees a live pool.
    if unsafe { (*pool).cpu_slots.is_null() } {
        return;
    }

    // SAFETY: The caller guarantees no remaining users of the slots.
    unsafe { free_slots(pool) };

    // SAFETY: The caller guarantees a live pool.
    if let Some(release) = unsafe { (*pool).release } {
        // SAFETY: The caller guarantees a live pool and context.
        unsafe { release(pool, (*pool).context) };
    }
}

/// Initializes an object pool and preallocates all objects.
///
/// # Safety
///
/// `pool` must point to writable storage for an `objpool_head`. `context` and
/// the callbacks must remain valid for all operations on the resulting pool.
#[export]
unsafe extern "C" fn objpool_init(
    pool: *mut Pool,
    nr_objs: c_int,
    object_size: c_int,
    gfp: bindings::gfp_t,
    context: *mut c_void,
    objinit: InitCallback,
    release: FiniCallback,
) -> c_int {
    let nr_objects = nr_objs as u32;
    let object_size_value = object_size as u32;
    if nr_objs <= 0
        || nr_objects > bindings::OBJPOOL_NR_OBJECT_MAX
        || object_size <= 0
        || object_size_value > bindings::OBJPOOL_OBJECT_SIZE_MAX
    {
        return INVALID;
    }

    let align = mem::size_of::<c_ulong>();
    let aligned_object_size = (object_size as usize)
        .checked_add(align - 1)
        .map(|size| size & !(align - 1));
    let Some(aligned_object_size) = aligned_object_size else {
        return INVALID;
    };

    let Some(capacity) = nr_objects.checked_next_power_of_two() else {
        return INVALID;
    };
    if capacity == 0 || capacity > c_int::MAX as u32 {
        return INVALID;
    }

    // SAFETY: The caller guarantees writable pool storage.
    unsafe { ptr::write_bytes(pool.cast::<u8>(), 0, mem::size_of::<Pool>()) };
    // SAFETY: The caller guarantees writable pool storage.
    let nr_possible_cpus = unsafe { bindings::rust_objpool_num_possible_cpus() };
    if nr_possible_cpus <= 0 {
        return INVALID;
    }
    // SAFETY: This helper exposes the kernel CPU-count boundary.
    let nr_cpu_ids = unsafe { bindings::rust_objpool_nr_cpu_ids() };
    if nr_cpu_ids <= 0 {
        return INVALID;
    }
    // SAFETY: The caller guarantees writable pool storage.
    unsafe {
        (*pool).nr_possible_cpus = nr_possible_cpus;
        (*pool).obj_size = aligned_object_size as c_int;
        (*pool).capacity = capacity as c_int;
        (*pool).gfp = gfp & !bindings::__GFP_ZERO;
        (*pool).context = context;
        (*pool).release = release;
    }

    let Some(slot_array_size) = (nr_cpu_ids as usize).checked_mul(mem::size_of::<*mut Slot>())
    else {
        return NO_MEMORY;
    };
    // SAFETY: The caller provides the public allocation context.
    let slots = unsafe { bindings::rust_objpool_alloc_cpu_slots(slot_array_size, (*pool).gfp) };
    if slots.is_null() {
        return NO_MEMORY;
    }
    // SAFETY: The caller guarantees writable pool storage.
    unsafe { (*pool).cpu_slots = slots.cast::<*mut Slot>() };

    // SAFETY: The pool is initialized and `nr_objs` passed validation.
    let result = unsafe { init_slots(pool, nr_objs as usize, context, objinit) };
    if result != 0 {
        return result;
    }

    // SAFETY: Every requested object was initialized.
    let references = unsafe { (*pool).nr_objs } + 1;
    // SAFETY: The caller guarantees a live initialized pool.
    unsafe { bindings::rust_objpool_refcount_set(pool, references) };
    0
}

/// Forcibly releases all pool storage and invokes the cleanup callback.
///
/// # Safety
///
/// `pool` must be a live initialized pool with no remaining users.
#[export]
unsafe extern "C" fn objpool_free(pool: *mut Pool) {
    // SAFETY: The caller provides the public forced-release contract.
    unsafe { free_pool(pool) };
}

/// Drops one outstanding object and releases the pool on the final reference.
///
/// # Safety
///
/// `obj` must be an outstanding object from `pool`, and `pool` must remain
/// live until this call completes.
#[export]
unsafe extern "C" fn objpool_drop(obj: *mut c_void, pool: *mut Pool) -> c_int {
    if obj.is_null() || pool.is_null() {
        return INVALID;
    }

    // SAFETY: The caller guarantees a live pool and outstanding reference.
    if unsafe { bindings::rust_objpool_refcount_dec_and_test(pool) } {
        // SAFETY: The final reference was just released.
        unsafe { free_pool(pool) };
        0
    } else {
        -(bindings::EAGAIN as c_int)
    }
}

/// Drains free objects and drops the pool's own reference.
///
/// # Safety
///
/// `pool` must be live and every producer must have stopped before this call.
#[export]
unsafe extern "C" fn objpool_fini(pool: *mut Pool) {
    let mut references: c_int = 1;

    loop {
        // SAFETY: The caller guarantees a live initialized pool.
        if unsafe { bindings::rust_objpool_pop(pool) }.is_null() {
            break;
        }
        references += 1;
    }

    // SAFETY: The caller guarantees a live pool and a matching reference set.
    if unsafe { bindings::rust_objpool_refcount_sub_and_test(pool, references) } {
        // SAFETY: The pool's own and all free-object references were released.
        unsafe { free_pool(pool) };
    }
}
