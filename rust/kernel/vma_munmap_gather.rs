// SPDX-License-Identifier: GPL-2.0

//! Native VMA gathering for aligned unmapping.
//!
//! Rust owns split decisions, VMA iteration, detached-tree insertion order,
//! accounting accumulation, userfaultfd preparation, and rollback. C retains
//! the per-VMA split, maple-tree, lock, reverse-map, and page-table primitives.

use super::vma_munmap_complete::reattach;
use crate::bindings;
use core::ffi::c_int;
use macros::export;

type GatherState = bindings::rust_vma_munmap_gather_state;

/// Gathers and isolates every VMA touched by one aligned unmap range.
///
/// # Safety
///
/// The caller must hold the mmap write lock and initialize `state` with a live
/// iterator, first VMA, and detached tree.
unsafe fn gather(state: *mut GatherState) -> c_int {
    // SAFETY: The caller upholds the gather-state requirements. Every helper
    // below operates on the supplied iterator, VMA, or detached tree.
    unsafe {
        let state = &mut *state;
        let first = state.first;
        let first_start = bindings::rust_helper_vma_munmap_vma_start(first);
        let first_end = bindings::rust_helper_vma_munmap_vma_end(first);

        if state.start > first_start {
            let map_count = bindings::rust_helper_vma_munmap_map_count(first);
            let max_map_count = bindings::rust_helper_vma_munmap_max_map_count();
            if state.end < first_end && map_count >= max_map_count {
                return -(bindings::ENOMEM as c_int);
            }

            if bindings::rust_helper_vma_munmap_vma_is_sealed(first) {
                return -(bindings::EPERM as c_int);
            }

            let error = bindings::rust_helper_vma_munmap_split(state.vmi, first, state.start, true);
            if error != 0 {
                return error;
            }
        }

        state.prev = bindings::rust_helper_vma_munmap_prev(state.vmi);
        if !state.prev.is_null() {
            state.unmap_start = bindings::rust_helper_vma_munmap_vma_end(state.prev);
        }

        loop {
            let vma = bindings::rust_helper_vma_munmap_find(state.vmi, state.end);
            if vma.is_null() {
                break;
            }

            if bindings::rust_helper_vma_munmap_vma_is_sealed(vma) {
                reattach(state.mas_detach);
                return -(bindings::EPERM as c_int);
            }

            if bindings::rust_helper_vma_munmap_vma_end(vma) > state.end {
                let error =
                    bindings::rust_helper_vma_munmap_split(state.vmi, vma, state.end, false);
                if error != 0 {
                    reattach(state.mas_detach);
                    return error;
                }
            }

            bindings::rust_helper_vma_munmap_start_write(vma);
            let index = state.vma_count;
            state.vma_count = state.vma_count.wrapping_add(1);
            bindings::rust_helper_vma_munmap_mas_set(state.mas_detach, index);
            let error = bindings::rust_helper_vma_munmap_mas_store(state.mas_detach, vma);
            if error != 0 {
                reattach(state.mas_detach);
                return error;
            }

            bindings::rust_helper_vma_munmap_mark_detached(vma);
            let nr_pages = bindings::rust_helper_vma_munmap_vma_pages(vma);
            state.nr_pages = state.nr_pages.wrapping_add(nr_pages);

            let flags = bindings::rust_helper_vma_munmap_vma_flags(vma);
            if flags & bindings::rust_vma_munmap_vma_flags_RUST_VMA_MUNMAP_VMA_LOCKED != 0 {
                state.locked_vm = state.locked_vm.wrapping_add(nr_pages);
            }
            if flags & bindings::rust_vma_munmap_vma_flags_RUST_VMA_MUNMAP_VMA_ACCOUNTED != 0 {
                state.nr_accounted = state.nr_accounted.wrapping_add(nr_pages);
            }

            match bindings::rust_helper_vma_munmap_account_kind(vma) {
                bindings::rust_vma_munmap_account_kind_RUST_VMA_MUNMAP_ACCOUNT_EXEC => {
                    state.exec_vm = state.exec_vm.wrapping_add(nr_pages);
                }
                bindings::rust_vma_munmap_account_kind_RUST_VMA_MUNMAP_ACCOUNT_STACK => {
                    state.stack_vm = state.stack_vm.wrapping_add(nr_pages);
                }
                bindings::rust_vma_munmap_account_kind_RUST_VMA_MUNMAP_ACCOUNT_DATA => {
                    state.data_vm = state.data_vm.wrapping_add(nr_pages);
                }
                _ => {}
            }

            if !state.uf.is_null() {
                let error = bindings::rust_helper_vma_munmap_userfault_prep(
                    vma,
                    state.start,
                    state.end,
                    state.uf,
                );
                if error != 0 {
                    reattach(state.mas_detach);
                    return error;
                }
            }

            bindings::rust_helper_vma_munmap_debug_check_vma(vma, state.start, state.end);
        }

        state.next = bindings::rust_helper_vma_munmap_next(state.vmi);
        if !state.next.is_null() {
            state.unmap_end = bindings::rust_helper_vma_munmap_vma_start(state.next);
        }

        bindings::rust_helper_vma_munmap_debug_gather(state);
        while bindings::rust_helper_vma_munmap_iter_addr(state.vmi) > state.start {
            bindings::rust_helper_vma_munmap_iter_prev_range(state.vmi);
        }

        state.clear_ptes = true;
        0
    }
}

/// Gathers and isolates VMAs for one aligned unmap range.
///
/// # Safety
///
/// The caller must hold the mmap write lock and initialize `state` with a live
/// iterator, first VMA, and detached tree. The caller owns the detached tree
/// after this function returns.
#[export]
unsafe extern "C" fn rust_vma_munmap_gather(state: *mut GatherState) -> c_int {
    // SAFETY: The caller upholds the gather-state requirements.
    unsafe { gather(state) }
}
