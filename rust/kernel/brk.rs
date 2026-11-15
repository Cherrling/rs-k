// SPDX-License-Identifier: GPL-2.0

//! Native orchestration for the `brk` syscall.
//!
//! Rust owns minimum break selection, data-limit validation, page-aligned
//! transition selection, shrink and expansion decisions, stack-gap rejection,
//! lock release, userfaultfd completion, and population. C retains the
//! existing address-space primitives and VMA operations.

use crate::bindings;
use crate::page::{page_align, PAGE_SIZE};
use core::ffi::c_int;
use macros::export;

type State = bindings::rust_brk_state;

fn error_value(error: c_int) -> usize {
    error as usize
}

fn errno(error: u32) -> c_int {
    error as c_int
}

/// Completes a successful operation after Rust still holds the write lock.
///
/// # Safety
///
/// `state` must be live, its mmap write lock must be held, and the iterator
/// and userfaultfd list must no longer be in use.
unsafe fn finish_locked(
    state: *mut State,
    requested: usize,
    oldbrk: usize,
    newbrk: usize,
    populate: bool,
) -> usize {
    // SAFETY: The caller upheld the lock and state requirements.
    unsafe {
        bindings::rust_helper_brk_write_unlock(state);
        bindings::rust_helper_brk_userfault_complete(state);
        if populate {
            bindings::rust_helper_brk_populate(oldbrk, newbrk.wrapping_sub(oldbrk));
        }
        requested
    }
}

/// Restores the original break and releases the write lock.
///
/// # Safety
///
/// `state` must be live and its mmap write lock must be held.
unsafe fn fail_locked(state: *mut State, original: usize) -> usize {
    // SAFETY: The caller upheld the lock and state requirements.
    unsafe {
        bindings::rust_helper_brk_set_address(state, original);
        bindings::rust_helper_brk_write_unlock(state);
        original
    }
}

/// Handles a page-aligned shrink transition.
///
/// # Safety
///
/// `state` must be live and its mmap write lock must be held.
unsafe fn shrink(
    state: *mut State,
    requested: usize,
    original: usize,
    oldbrk: usize,
    newbrk: usize,
) -> usize {
    // SAFETY: The caller upheld the lock and state requirements.
    unsafe {
        bindings::rust_helper_brk_iterator_init(state, newbrk);
        let vma = bindings::rust_helper_brk_find(state, oldbrk);
        if vma.is_null() || bindings::rust_helper_brk_vma_start(vma) >= oldbrk {
            return fail_locked(state, original);
        }

        bindings::rust_helper_brk_set_address(state, requested);
        if bindings::rust_helper_brk_align_unmap(state, vma, newbrk, oldbrk) != 0 {
            return fail_locked(state, original);
        }

        bindings::rust_helper_brk_userfault_complete(state);
        requested
    }
}

/// Handles a page-aligned expansion transition.
///
/// # Safety
///
/// `state` must be live and its mmap write lock must be held.
unsafe fn expand(
    state: *mut State,
    requested: usize,
    original: usize,
    oldbrk: usize,
    newbrk: usize,
) -> usize {
    // SAFETY: The caller upheld the lock and state requirements.
    unsafe {
        if bindings::rust_helper_brk_check_limits(oldbrk, newbrk.wrapping_sub(oldbrk)) != 0 {
            return fail_locked(state, original);
        }

        bindings::rust_helper_brk_iterator_init(state, oldbrk);
        let search_end = newbrk
            .wrapping_add(PAGE_SIZE)
            .wrapping_add(bindings::rust_helper_brk_stack_guard_gap());
        let next = bindings::rust_helper_brk_find(state, search_end);
        if !next.is_null()
            && newbrk.wrapping_add(PAGE_SIZE) > bindings::rust_helper_brk_vma_start_gap(next)
        {
            return fail_locked(state, original);
        }

        let vma = bindings::rust_helper_brk_vma_prev(state);
        if bindings::rust_helper_brk_expand(state, vma, oldbrk, newbrk.wrapping_sub(oldbrk)) < 0 {
            return fail_locked(state, original);
        }

        bindings::rust_helper_brk_set_address(state, requested);
        let populate = bindings::rust_helper_brk_should_populate(state);
        finish_locked(state, requested, oldbrk, newbrk, populate)
    }
}

/// Runs the complete break transition.
///
/// # Safety
///
/// The caller must pass a live private state for the current address space.
/// This function acquires and releases the mmap write lock.
unsafe fn do_brk(state: *mut State) -> usize {
    // SAFETY: The caller supplied a live state for the current address space.
    unsafe {
        if bindings::rust_helper_brk_write_lock_killable(state) != 0 {
            return error_value(-errno(bindings::EINTR));
        }

        let original = bindings::rust_helper_brk_current(state);
        let start = bindings::rust_helper_brk_start(state);
        let min_brk = if bindings::rust_helper_brk_use_end_data() {
            bindings::rust_helper_brk_end_data(state)
        } else {
            start
        };
        let requested = (*state).brk;

        if requested < min_brk || bindings::rust_helper_brk_check_data_rlimit(state) != 0 {
            return fail_locked(state, original);
        }

        let newbrk = page_align(requested).unwrap_or(0);
        let oldbrk = page_align(original).unwrap_or(0);
        if oldbrk == newbrk {
            bindings::rust_helper_brk_set_address(state, requested);
            return finish_locked(state, requested, oldbrk, newbrk, false);
        }

        if requested <= original {
            return shrink(state, requested, original, oldbrk, newbrk);
        }

        expand(state, requested, original, oldbrk, newbrk)
    }
}

/// Runs the complete break transition.
///
/// # Safety
///
/// The caller must satisfy the requirements of [`do_brk`].
#[export]
unsafe extern "C" fn rust_brk(state: *mut State) -> usize {
    // SAFETY: The C adapter upholds the state and address-space requirements.
    unsafe { do_brk(state) }
}
