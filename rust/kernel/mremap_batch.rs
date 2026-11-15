// SPDX-License-Identifier: GPL-2.0

//! Native orchestration for moving multiple VMAs during `mremap`.
//!
//! Rust owns the batch loop, gap handling, operation order, error propagation,
//! iterator invalidation, and destination progression. C retains the VMA
//! iterator, field projections, predicates, and existing remap operations.

use crate::bindings;
use core::cmp::{max, min};
use core::ffi::c_int;
use macros::export;

type State = bindings::rust_mremap_batch_state;

fn errno(error: u32) -> c_int {
    error as c_int
}

fn error_value(error: c_int) -> usize {
    error as usize
}

/// Moves all VMAs in one fixed-address remap range.
///
/// # Safety
///
/// The caller supplies a live remap state, private iterator state, and the
/// current address space with its mmap write lock held.
unsafe fn remap_move(state: *mut State) -> usize {
    // SAFETY: The caller supplies a live remap state.
    unsafe {
        let start = bindings::rust_helper_mremap_batch_start(state);
        let end = bindings::rust_helper_mremap_batch_end(state);
        let initial_new_addr = bindings::rust_helper_mremap_batch_new_addr(state);
        let mut target_addr = initial_new_addr;
        let mut result = error_value(-errno(bindings::EFAULT));
        let mut last_end = 0usize;
        let mut seen_vma = false;

        loop {
            let vma = bindings::rust_helper_mremap_batch_next(state);
            if vma.is_null() {
                break;
            }

            let vma_start = bindings::rust_helper_mremap_batch_vma_start(vma);
            let vma_end = bindings::rust_helper_mremap_batch_vma_end(vma);
            let addr = max(vma_start, start);
            let len = min(end, vma_end).wrapping_sub(addr);

            if !seen_vma && start < vma_start {
                return error_value(-errno(bindings::EFAULT));
            }

            let offset = if seen_vma {
                vma_start.wrapping_sub(last_end)
            } else {
                0
            };
            last_end = vma_end;

            bindings::rust_helper_mremap_batch_configure(
                state,
                vma,
                addr,
                target_addr.wrapping_add(offset),
                len,
            );

            let multi_allowed = bindings::rust_helper_mremap_batch_multi_allowed(vma);
            if !multi_allowed {
                if seen_vma {
                    return error_value(-errno(bindings::EFAULT));
                }
                if vma_end < end {
                    return error_value(-errno(bindings::EFAULT));
                }
            }

            let mut res_vma = bindings::rust_helper_mremap_batch_check_prepare(state);
            if res_vma == 0 {
                res_vma = bindings::rust_helper_mremap_batch_mremap_to(state);
            }
            if bindings::rust_helper_mremap_batch_is_error_value(res_vma) {
                return res_vma;
            }

            if !seen_vma {
                bindings::rust_helper_mremap_batch_warn_multi_destination(
                    multi_allowed && res_vma != initial_new_addr,
                );
                result = res_vma;
            }

            bindings::rust_helper_mremap_batch_warn_locked(
                !bindings::rust_helper_mremap_batch_mmap_locked(state),
            );
            bindings::rust_helper_mremap_batch_warn_populate(
                bindings::rust_helper_mremap_batch_populate_expand(state),
            );

            if bindings::rust_helper_mremap_batch_needs_invalidate(state) {
                bindings::rust_helper_mremap_batch_invalidate(state);
            }

            seen_vma = true;
            target_addr = res_vma.wrapping_add(bindings::rust_helper_mremap_batch_new_len(state));
        }

        result
    }
}

/// Moves all VMAs in one fixed-address remap range.
///
/// # Safety
///
/// The caller must satisfy the requirements of [`remap_move`].
#[export]
unsafe extern "C" fn rust_mremap_batch(state: *mut State) -> usize {
    // SAFETY: The caller upholds the batch move requirements.
    unsafe { remap_move(state) }
}
