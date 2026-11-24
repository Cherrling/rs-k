// SPDX-License-Identifier: GPL-2.0-only

//! Native DMA coherent pool allocation.
//!
//! Rust owns argument validation, alignment and boundary arithmetic, pool and
//! page lifecycle decisions, block free-list transitions, busy-pool handling,
//! and managed-device release policy. C retains the shared layouts, locking
//! primitives, coherent DMA allocation, sysfs and diagnostics, device-resource
//! primitives, and the original non-GPL export metadata.

use crate::{
    bindings,
    ffi::{c_char, c_int, c_ulong, c_void},
};
use core::{cmp, ptr};
use macros::export;

type Block = bindings::dma_block;
type Device = bindings::device;
type DmaAddr = bindings::dma_addr_t;
type Gfp = bindings::gfp_t;
type Page = bindings::dma_page;
type Pool = bindings::dma_pool;

const INT_MAX: usize = c_int::MAX as usize;
const NUMA_NO_NODE: c_int = bindings::NUMA_NO_NODE;
const PAGE_SIZE: usize = bindings::PAGE_SIZE;

/// Holds the pool spinlock with interrupts disabled.
struct PoolLock {
    pool: *mut Pool,
    flags: c_ulong,
}

impl PoolLock {
    /// Acquires the lock that protects the pool free list.
    ///
    /// # Safety
    ///
    /// `pool` must remain valid while the guard exists.
    unsafe fn new(pool: *mut Pool) -> Self {
        // SAFETY: The caller guarantees a live pool.
        let flags = unsafe { bindings::rust_dmapool_lock(pool) };
        Self { pool, flags }
    }
}

impl Drop for PoolLock {
    fn drop(&mut self) {
        // SAFETY: This guard owns the matching lock acquisition and the pool
        // remains live for the complete function call.
        unsafe { bindings::rust_dmapool_unlock(self.pool, self.flags) };
    }
}

/// Removes the first free block from a locked pool.
///
/// # Safety
///
/// The caller must hold the pool lock and `pool` must be live.
unsafe fn block_pop(pool: *mut Pool) -> Option<*mut Block> {
    // SAFETY: The caller guarantees the lock and live pool.
    let block = unsafe { (*pool).next_block };
    if block.is_null() {
        return None;
    }

    // SAFETY: The free-list head points to a live block owned by this pool.
    unsafe {
        (*pool).next_block = (*block).next_block;
        (*pool).nr_active = (*pool).nr_active.wrapping_add(1);
    }
    Some(block)
}

/// Returns a block to the locked pool free list.
///
/// # Safety
///
/// The caller must hold the pool lock and both pointers must be live.
unsafe fn block_push(pool: *mut Pool, block: *mut Block, dma: DmaAddr) {
    // SAFETY: The caller guarantees the lock, live pool, and live block.
    unsafe {
        (*block).dma = dma;
        (*block).next_block = (*pool).next_block;
        (*pool).next_block = block;
        (*pool).nr_active = (*pool).nr_active.wrapping_sub(1);
    }
}

/// Builds the initial free list for a newly allocated page.
///
/// # Safety
///
/// The caller must hold the pool lock. `pool` and `page` must be live and the
/// page must not already be linked.
unsafe fn initialise_page(pool: *mut Pool, page: *mut Page) {
    // SAFETY: The caller guarantees live pool and page objects.
    let boundary = unsafe { (*pool).boundary };
    // SAFETY: The caller guarantees live pool and page objects.
    let size = unsafe { (*pool).size };
    // SAFETY: The caller guarantees live pool and page objects.
    let allocation = unsafe { (*pool).allocation };
    let mut next_boundary = boundary;
    let mut offset: u32 = 0;
    let mut first: *mut Block = ptr::null_mut();
    let mut last: *mut Block = ptr::null_mut();

    // SAFETY: The helper applies the configured debug or zeroing policy.
    unsafe { bindings::rust_dmapool_poison_page(pool, page) };

    while offset.wrapping_add(size) <= allocation {
        if offset.wrapping_add(size) > next_boundary {
            offset = next_boundary;
            next_boundary = next_boundary.wrapping_add(boundary);
            continue;
        }

        // SAFETY: The page allocation is large enough for this fragment.
        let block = unsafe { (*page).vaddr.cast::<u8>().add(offset as usize) }.cast::<Block>();
        // SAFETY: The block lies inside the live page allocation.
        unsafe {
            (*block).dma = (*page).dma.wrapping_add(offset.into());
            (*block).next_block = ptr::null_mut();
        }

        if !last.is_null() {
            // SAFETY: `last` is the previous block in this page.
            unsafe { (*last).next_block = block };
        } else {
            first = block;
        }
        last = block;
        offset = offset.wrapping_add(size);
        // SAFETY: The caller holds the pool lock.
        unsafe { (*pool).nr_blocks = (*pool).nr_blocks.wrapping_add(1) };
    }

    debug_assert!(!last.is_null());
    if !last.is_null() {
        // SAFETY: Both pointers are live and the caller holds the pool lock.
        unsafe {
            (*last).next_block = (*pool).next_block;
            (*pool).next_block = first;
        }
    }
    // SAFETY: The page is live and the caller holds the pool lock.
    unsafe {
        bindings::rust_dmapool_add_page(pool, page);
        (*pool).nr_pages = (*pool).nr_pages.wrapping_add(1);
    }
}

/// Creates a DMA pool on a specific NUMA node.
///
/// # Safety
///
/// `name` must be a valid NUL-terminated string. `dev` must remain live until
/// the returned pool is destroyed.
unsafe fn create_node(
    name: *const c_char,
    dev: *mut Device,
    mut size: usize,
    mut align: usize,
    mut boundary: usize,
    node: c_int,
) -> *mut Pool {
    if dev.is_null() {
        return ptr::null_mut();
    }

    if align == 0 {
        align = 1;
    } else if align & align.wrapping_sub(1) != 0 {
        return ptr::null_mut();
    }

    if size == 0 || size > INT_MAX {
        return ptr::null_mut();
    }

    let block_size = core::mem::size_of::<Block>();
    if size < block_size {
        size = block_size;
    }
    size = size.wrapping_add(align - 1) & !align.wrapping_sub(1);

    let allocation = cmp::max(size, PAGE_SIZE);
    if boundary == 0 {
        boundary = allocation;
    } else if boundary < size || boundary & boundary.wrapping_sub(1) != 0 {
        return ptr::null_mut();
    }
    boundary = cmp::min(boundary, allocation);

    // SAFETY: The helper performs a zeroed kernel allocation.
    let pool = unsafe { bindings::rust_dmapool_alloc_pool_struct(node) };
    if pool.is_null() {
        return pool;
    }

    // SAFETY: The new pool is exclusively owned by this function.
    unsafe {
        bindings::rust_dmapool_set_pool_name(pool, name);
        bindings::rust_dmapool_init_pool(pool);
        (*pool).dev = dev;
        (*pool).size = size as u32;
        (*pool).boundary = boundary as u32;
        (*pool).allocation = allocation as u32;
        (*pool).node = node;

        bindings::rust_dmapool_reg_lock();
        bindings::rust_dmapool_pools_lock();
    }
    // SAFETY: The device and its DMA pool list remain live.
    let empty = unsafe { bindings::rust_dmapool_dev_has_no_pools(dev) };
    // SAFETY: The new pool is exclusively owned and initialized.
    unsafe { bindings::rust_dmapool_add_pool(pool) };
    // SAFETY: This balances the lock acquired above.
    unsafe { bindings::rust_dmapool_pools_unlock() };

    if empty {
        // SAFETY: The device remains live and the pool is linked.
        let err = unsafe { bindings::rust_dmapool_create_pools_attr(dev) };
        if err != 0 {
            // SAFETY: The device and its DMA pool list remain live.
            unsafe { bindings::rust_dmapool_pools_lock() };
            // SAFETY: The pool was linked above.
            unsafe { bindings::rust_dmapool_del_pool(pool) };
            // SAFETY: This balances the pools lock.
            unsafe { bindings::rust_dmapool_pools_unlock() };
            // SAFETY: This balances the registration lock.
            unsafe { bindings::rust_dmapool_reg_unlock() };
            // SAFETY: Registration failed, so the pool has no external user.
            unsafe { bindings::rust_dmapool_free_pool_struct(pool) };
            return ptr::null_mut();
        }
    }

    // SAFETY: This balances the registration lock.
    unsafe { bindings::rust_dmapool_reg_unlock() };
    pool
}

/// Destroys a DMA pool and all of its pages.
///
/// # Safety
///
/// The caller guarantees that no block or operation uses `pool`.
unsafe fn destroy(pool: *mut Pool) {
    if pool.is_null() {
        return;
    }

    // SAFETY: The caller transfers exclusive pool ownership and the device
    // remains live until destruction completes.
    unsafe {
        bindings::rust_dmapool_reg_lock();
        bindings::rust_dmapool_pools_lock();
        bindings::rust_dmapool_del_pool(pool);
    }
    // SAFETY: The device remains live and the pool is linked.
    let empty = unsafe { bindings::rust_dmapool_dev_has_no_pools((*pool).dev) };
    // SAFETY: This balances the pools lock.
    unsafe { bindings::rust_dmapool_pools_unlock() };
    if empty {
        // SAFETY: The device remains live until all pools are gone.
        unsafe { bindings::rust_dmapool_remove_pools_attr((*pool).dev) };
    }
    // SAFETY: This balances the registration lock.
    unsafe { bindings::rust_dmapool_reg_unlock() };

    // SAFETY: The pool is exclusively owned by this function.
    let busy = unsafe { (*pool).nr_active != 0 };
    if busy {
        // SAFETY: The pool is live and exclusive.
        unsafe { bindings::rust_dmapool_report_busy(pool) };
    }

    // SAFETY: The pool is live and exclusive.
    let mut page = unsafe { bindings::rust_dmapool_first_page(pool) };
    while !page.is_null() {
        // SAFETY: The page is linked in the live pool.
        let next = unsafe { bindings::rust_dmapool_next_page(pool, page) };
        if !busy {
            // SAFETY: The caller guarantees that no pool allocation is active.
            unsafe { bindings::rust_dmapool_free_page(pool, page) };
        }
        // SAFETY: The page is linked and exclusively owned.
        unsafe {
            bindings::rust_dmapool_del_page(page);
            bindings::rust_dmapool_free_page_struct(page);
        }
        page = next;
    }

    // SAFETY: All pages and registrations are gone.
    unsafe { bindings::rust_dmapool_free_pool_struct(pool) };
}

/// Allocates one block from a DMA pool.
///
/// # Safety
///
/// `pool` must remain live and `handle` must point to writable DMA-address
/// storage.
unsafe fn alloc(pool: *mut Pool, mem_flags: Gfp, handle: *mut DmaAddr) -> *mut c_void {
    // SAFETY: The helper evaluates the allocation-context contract.
    unsafe { bindings::rust_dmapool_might_alloc(mem_flags) };

    // SAFETY: The caller guarantees a live pool.
    let lock = unsafe { PoolLock::new(pool) };
    // SAFETY: The pool is live and the lock is held by the guard.
    let mut block = unsafe { block_pop(pool) };
    drop(lock);

    if block.is_none() {
        // SAFETY: The pool remains live and allocation happens without its
        // spinlock held.
        let page =
            unsafe { bindings::rust_dmapool_alloc_page(pool, mem_flags & !bindings::__GFP_ZERO) };
        if page.is_null() {
            return ptr::null_mut();
        }

        // SAFETY: The pool remains live and the new page is exclusively owned.
        let lock = unsafe { PoolLock::new(pool) };
        // SAFETY: The pool lock is held and the page is publishable.
        unsafe { initialise_page(pool, page) };
        // SAFETY: The pool lock is held and initialization published a block.
        block = unsafe { block_pop(pool) };
        drop(lock);
    }

    let block = block.expect("dma pool page must contain a block");
    // SAFETY: The block is checked out from the live pool.
    let dma = unsafe { (*block).dma };
    // SAFETY: The caller supplied writable DMA-address storage.
    unsafe { handle.write(dma) };
    // SAFETY: The block was removed from the free list and remains live.
    unsafe { bindings::rust_dmapool_check_block(pool, block, mem_flags) };
    // SAFETY: The block remains checked out from the pool.
    unsafe { bindings::rust_dmapool_zero_block(pool, block, mem_flags) };

    block.cast::<c_void>()
}

/// Returns one block to a DMA pool.
///
/// # Safety
///
/// The caller guarantees that the block is not in use.
unsafe fn free(pool: *mut Pool, vaddr: *mut c_void, dma: DmaAddr) {
    // SAFETY: The caller guarantees a live pool.
    let _lock = unsafe { PoolLock::new(pool) };
    // SAFETY: The caller guarantees that `vaddr` and `dma` identify a block
    // owned by this pool.
    if !unsafe { bindings::rust_dmapool_block_error(pool, vaddr, dma) } {
        // SAFETY: The block is live and is not already on the free list.
        unsafe { block_push(pool, vaddr.cast::<Block>(), dma) };
    }
}

/// Implements `dma_pool_create_node()`.
///
/// # Safety
///
/// The public DMA pool creation contract applies.
#[export]
unsafe extern "C" fn dma_pool_create_node(
    name: *const c_char,
    dev: *mut Device,
    size: usize,
    align: usize,
    boundary: usize,
    node: c_int,
) -> *mut Pool {
    // SAFETY: Guaranteed by the public contract.
    unsafe { create_node(name, dev, size, align, boundary, node) }
}

/// Implements `dma_pool_destroy()`.
///
/// # Safety
///
/// The public DMA pool destruction contract applies.
#[export]
unsafe extern "C" fn dma_pool_destroy(pool: *mut Pool) {
    // SAFETY: Guaranteed by the public contract.
    unsafe { destroy(pool) };
}

/// Implements `dma_pool_alloc()`.
///
/// # Safety
///
/// The public DMA pool allocation contract applies.
#[export]
unsafe extern "C" fn dma_pool_alloc(
    pool: *mut Pool,
    mem_flags: Gfp,
    handle: *mut DmaAddr,
) -> *mut c_void {
    // SAFETY: Guaranteed by the public contract.
    unsafe { alloc(pool, mem_flags, handle) }
}

/// Implements `dma_pool_free()`.
///
/// # Safety
///
/// The public DMA pool free contract applies.
#[export]
unsafe extern "C" fn dma_pool_free(pool: *mut Pool, vaddr: *mut c_void, dma: DmaAddr) {
    // SAFETY: Guaranteed by the public contract.
    unsafe { free(pool, vaddr, dma) };
}

/// Implements `dmam_pool_create()`.
///
/// # Safety
///
/// The public managed DMA pool creation contract applies.
#[export]
unsafe extern "C" fn dmam_pool_create(
    name: *const c_char,
    dev: *mut Device,
    size: usize,
    align: usize,
    boundary: usize,
) -> *mut Pool {
    // SAFETY: The helper allocates a managed-resource slot.
    let slot = unsafe { bindings::rust_dmapool_devres_alloc() }.cast::<*mut Pool>();
    if slot.is_null() {
        return ptr::null_mut();
    }

    // SAFETY: Guaranteed by the public contract.
    let pool = unsafe { create_node(name, dev, size, align, boundary, NUMA_NO_NODE) };
    // SAFETY: The resource slot is exclusively owned and points to writable
    // managed-resource storage.
    unsafe { slot.write(pool) };
    if pool.is_null() {
        // SAFETY: The slot was allocated above and no resource was added.
        unsafe { bindings::rust_dmapool_devres_free(slot.cast()) };
    } else {
        // SAFETY: The device and resource slot remain live.
        unsafe { bindings::rust_dmapool_devres_add(dev, slot.cast()) };
    }

    pool
}

/// Implements `dmam_pool_destroy()`.
///
/// # Safety
///
/// The public managed DMA pool destruction contract applies.
#[export]
unsafe extern "C" fn dmam_pool_destroy(pool: *mut Pool) {
    // SAFETY: The public contract guarantees a live managed pool.
    let dev = unsafe { (*pool).dev };
    // SAFETY: The helper releases the matching managed resource.
    unsafe { bindings::rust_dmapool_devres_release(dev, pool) };
}
