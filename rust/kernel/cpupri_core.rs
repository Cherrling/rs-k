// SPDX-License-Identifier: GPL-2.0-only

//! Native CPU-priority migration state machine.
//!
//! Rust owns scheduler priority conversion, lowest-priority vector search,
//! task-affinity intersection, active-CPU filtering, fitness filtering, and
//! the ordered transition between priority vectors. C retains cpumask storage
//! allocation, cpumask primitives, atomics and barriers, and the public
//! scheduler entry-point adapters.

use crate::{
    bindings,
    ffi::{c_int, c_uint},
};
use core::ptr;

type Cpupri = bindings::cpupri;
type Cpumask = bindings::cpumask;
type Task = bindings::task_struct;
type FitnessFn = unsafe extern "C" fn(*mut Task, c_int) -> bool;

const INVALID: c_int = bindings::CPUPRI_INVALID as c_int;
const NORMAL: c_int = bindings::CPUPRI_NORMAL as c_int;
const HIGHER: c_int = bindings::CPUPRI_HIGHER as c_int;
const NR_PRIORITIES: c_int = bindings::CPUPRI_NR_PRIORITIES as c_int;

/// Converts a scheduler priority to the matching CPU-priority vector.
///
/// Lower vector indexes contain CPUs that are more eligible for pulling RT
/// work. Returns `None` for a priority outside the scheduler's RT contract.
fn convert_priority(priority: c_int) -> Option<c_int> {
    if priority == INVALID {
        Some(INVALID)
    } else if (0..HIGHER - 1).contains(&priority) {
        Some(HIGHER - 1 - priority)
    } else if priority == HIGHER - 1 {
        Some(NORMAL)
    } else if priority == HIGHER {
        Some(HIGHER)
    } else {
        None
    }
}

/// Returns the task affinity mask used by the CPU-priority search.
///
/// # Safety
///
/// `task` must point to a valid task whose affinity mask is stable for the
/// duration of the search.
#[inline]
unsafe fn task_mask(task: *mut Task) -> *const Cpumask {
    // SAFETY: The caller guarantees that `task` is valid.
    unsafe { ptr::addr_of!((*task).cpus_mask) }
}

/// Returns the vector mask for a CPU priority.
///
/// # Safety
///
/// `cp` must be initialized and `priority` must be a valid vector index.
#[inline]
unsafe fn vector_mask(cp: *mut Cpupri, priority: c_int) -> *mut Cpumask {
    // SAFETY: The caller guarantees that `cp` and `priority` are valid.
    unsafe { bindings::rust_cpupri_vec_mask(cp, priority) }
}

/// Returns the CPU count for a priority vector.
///
/// # Safety
///
/// `cp` must be initialized and `priority` must be a valid vector index.
#[inline]
unsafe fn vector_count(cp: *mut Cpupri, priority: c_int) -> *mut bindings::atomic_t {
    // SAFETY: The caller guarantees that `cp` and `priority` are valid.
    unsafe { ptr::addr_of_mut!((*cp).pri_to_cpu[priority as usize].count) }
}

/// Returns the currently active CPU mask.
#[inline]
fn active_cpu_mask() -> *mut Cpumask {
    ptr::addr_of_mut!(bindings::__cpu_active_mask)
}

/// Tests whether one priority vector intersects the task affinity mask.
///
/// The vector count is read before the mask. The read barrier prevents a
/// concurrently added mask from being missed after its count becomes visible.
///
/// # Safety
///
/// `cp` must be initialized, `task` must be valid, and `lowest_mask` must be
/// either null or point to writable cpumask storage.
unsafe fn find_vector(
    cp: *mut Cpupri,
    task: *mut Task,
    lowest_mask: *mut Cpumask,
    priority: c_int,
) -> bool {
    // SAFETY: The caller guarantees that the vector is initialized.
    let count = unsafe { bindings::atomic_read(vector_count(cp, priority)) };
    let skip = count == 0;

    // Match the original read barrier for every priority iteration.
    // SAFETY: This helper has no pointer arguments.
    unsafe { bindings::smp_rmb() };
    if skip {
        return false;
    }

    // SAFETY: The caller guarantees that `task` and the vector are valid.
    let task_mask = unsafe { task_mask(task) };
    // SAFETY: The caller guarantees that the vector is initialized.
    let mask = unsafe { vector_mask(cp, priority) };
    // SAFETY: The task and vector masks are valid for this search.
    if unsafe { bindings::cpumask_any_and(task_mask, mask) } >= unsafe { bindings::nr_cpu_ids } {
        return false;
    }

    if lowest_mask.is_null() {
        return true;
    }

    // SAFETY: The caller guarantees writable lowest-mask storage.
    unsafe { bindings::cpumask_and(lowest_mask, task_mask, mask) };
    // SAFETY: Both masks contain `nr_cpu_ids` valid bits.
    unsafe { bindings::cpumask_and(lowest_mask, lowest_mask, active_cpu_mask()) };

    // The vector mask can race with the first intersection. An empty result
    // means the CPU left this vector and the next priority must be checked.
    // SAFETY: `lowest_mask` is initialized by the intersections above.
    !unsafe { bindings::cpumask_empty(lowest_mask) }
}

/// Applies capacity-aware fitness filtering to a candidate CPU mask.
///
/// # Safety
///
/// `task` and `mask` must be valid, and `fitness` must accept `task` and the
/// CPUs in `mask`.
unsafe fn filter_fitness(task: *mut Task, mask: *mut Cpumask, fitness: FitnessFn) -> bool {
    // SAFETY: The caller guarantees that `mask` is valid.
    let nr_cpu_ids = unsafe { bindings::nr_cpu_ids };
    // SAFETY: `mask` is valid and `-1` starts iteration at the first CPU.
    let mut cpu = unsafe { bindings::cpumask_next(-1, mask) };

    while cpu < nr_cpu_ids {
        // SAFETY: The caller guarantees that the callback accepts this task
        // and every CPU represented by the candidate mask.
        if !unsafe { fitness(task, cpu as c_int) } {
            // SAFETY: `cpu` is set in the writable candidate mask.
            unsafe { bindings::cpumask_clear_cpu(cpu as c_int, mask) };
        }
        // SAFETY: `mask` remains valid after clearing a set bit.
        cpu = unsafe { bindings::cpumask_next(cpu as c_int, mask) };
    }

    // SAFETY: `mask` is valid.
    !unsafe { bindings::cpumask_empty(mask) }
}

/// Searches all lower CPU-priority vectors for the task.
///
/// # Safety
///
/// `cp` must be initialized, `task` must be valid, `lowest_mask` must be null
/// or writable, and `fitness` must satisfy the callback contract for `task`.
unsafe fn find_vectors(
    cp: *mut Cpupri,
    task: *mut Task,
    lowest_mask: *mut Cpumask,
    fitness: Option<FitnessFn>,
) -> bool {
    // SAFETY: The caller guarantees that `task` is valid.
    let task_priority = unsafe { (*task).prio };
    let Some(task_pri) = convert_priority(task_priority) else {
        if task_priority >= NR_PRIORITIES {
            // SAFETY: The helper only reads the priority value.
            unsafe { bindings::rust_cpupri_warn_bad_task_priority(task_priority) };
        }
        return false;
    };

    if task_pri >= NR_PRIORITIES {
        // SAFETY: The helper only reads the priority value.
        unsafe { bindings::rust_cpupri_warn_bad_task_priority(task_pri) };
    }

    for priority in 0..task_pri {
        // SAFETY: The caller guarantees the search context.
        if !unsafe { find_vector(cp, task, lowest_mask, priority) } {
            continue;
        }

        let Some(fitness) = fitness else {
            return true;
        };
        if lowest_mask.is_null() {
            return true;
        }

        // SAFETY: A matching vector filled the valid lowest mask.
        if unsafe { filter_fitness(task, lowest_mask, fitness) } {
            return true;
        }
    }

    if fitness.is_some() {
        // Preserve priority over capacity fitting if no candidate fits.
        // SAFETY: The caller guarantees the fallback search context.
        unsafe { find_vectors(cp, task, lowest_mask, None) }
    } else {
        false
    }
}

/// Finds a lower-priority set of CPUs for a task.
///
/// # Safety
///
/// `cp` must be initialized, `task` must be valid, and `lowest_mask` must be
/// null or point to writable cpumask storage.
#[no_mangle]
unsafe extern "C" fn rust_cpupri_find(
    cp: *mut Cpupri,
    task: *mut Task,
    lowest_mask: *mut Cpumask,
) -> c_int {
    // SAFETY: The caller provides the C entry-point contract.
    unsafe { c_int::from(find_vectors(cp, task, lowest_mask, None)) }
}

/// Finds CPUs for a task while applying a capacity fitness callback.
///
/// # Safety
///
/// The caller must provide the same contracts as `rust_cpupri_find`.
/// `fitness_fn` must be null or valid for `task` and every candidate CPU.
#[no_mangle]
unsafe extern "C" fn rust_cpupri_find_fitness(
    cp: *mut Cpupri,
    task: *mut Task,
    lowest_mask: *mut Cpumask,
    fitness_fn: Option<FitnessFn>,
) -> c_int {
    // SAFETY: The caller provides the C entry-point contract.
    unsafe { c_int::from(find_vectors(cp, task, lowest_mask, fitness_fn)) }
}

/// Changes one CPU between CPU-priority vectors.
///
/// # Safety
///
/// `cp` must be initialized, `cpu` must be a valid CPU index, and the caller
/// must hold the run-queue lock required by the scheduler contract.
unsafe fn set_priority(cp: *mut Cpupri, cpu: c_int, new_priority: c_int) {
    // SAFETY: The caller guarantees that `cp` and `cpu` are valid.
    let current = unsafe { (*cp).cpu_to_pri.add(cpu as usize) };
    // SAFETY: `cpu_to_pri` covers every possible CPU.
    let old_priority = unsafe { *current };
    if new_priority == old_priority {
        return;
    }

    let mut synchronized_update = false;
    if new_priority != INVALID {
        // Add the new vector before removing the old one. This prevents a
        // concurrent search from missing both vectors during a priority rise.
        // SAFETY: The caller guarantees that the new vector is initialized.
        unsafe { bindings::cpumask_set_cpu(cpu as c_uint, vector_mask(cp, new_priority)) };
        // SAFETY: This preserves the vector-mask-before-count protocol.
        unsafe { bindings::smp_mb__before_atomic() };
        // SAFETY: The vector count is valid.
        unsafe { bindings::atomic_inc(vector_count(cp, new_priority)) };
        synchronized_update = true;
    }

    if old_priority != INVALID {
        if synchronized_update {
            // Publish the new vector before removing the old vector.
            // SAFETY: This preserves the cross-vector transition protocol.
            unsafe { bindings::smp_mb__after_atomic() };
        }

        // Remove the count before the mask so a search never trusts a stale
        // non-zero count for a mask that is about to disappear.
        // SAFETY: The old vector count is valid.
        unsafe { bindings::atomic_dec(vector_count(cp, old_priority)) };
        // SAFETY: This preserves the count-before-mask removal protocol.
        unsafe { bindings::smp_mb__after_atomic() };
        // SAFETY: The old vector is initialized.
        unsafe { bindings::cpumask_clear_cpu(cpu, vector_mask(cp, old_priority)) };
    }

    // Publish the CPU mapping only after both vector updates are complete.
    // SAFETY: The caller holds the lock that protects this array entry.
    unsafe { *current = new_priority };
}

/// Updates a CPU priority setting.
///
/// # Safety
///
/// `cp` must be initialized, `cpu` must be a valid CPU index, and the caller
/// must hold the run-queue lock required by the scheduler contract.
#[no_mangle]
unsafe extern "C" fn rust_cpupri_set(cp: *mut Cpupri, cpu: c_int, new_priority: c_int) {
    let Some(converted) = convert_priority(new_priority) else {
        // SAFETY: The helper handles the invalid scheduler input.
        unsafe { bindings::rust_cpupri_bug_bad_priority(new_priority) };
        return;
    };

    // SAFETY: The caller provides the C entry-point contract.
    unsafe { set_priority(cp, cpu, converted) };
}
