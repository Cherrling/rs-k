// SPDX-License-Identifier: GPL-2.0

//! Native orchestration for changing user memory protections.
//!
//! Rust owns request validation, mmap-lock acquisition, growth-policy
//! selection, the production VMA loop, per-VMA permission conversion,
//! security ordering, memory accounting, VMA modification, page-protection
//! updates, and population decisions. C retains field projections, fixed flag
//! conversions, page-table walks, architecture predicates, and existing mm
//! primitives.

use crate::bindings;
use crate::page::{page_align, PAGE_MASK, PAGE_SHIFT};
use core::ffi::c_int;
use macros::export;

type State = bindings::rust_mprotect_state;
type Vma = bindings::vm_area_struct;

const PROT_READ: usize = bindings::rust_mprotect_constant_RUST_MPROTECT_PROT_READ as usize;
const PROT_EXEC: usize = bindings::rust_mprotect_constant_RUST_MPROTECT_PROT_EXEC as usize;
const PROT_GROWSDOWN: usize =
    bindings::rust_mprotect_constant_RUST_MPROTECT_PROT_GROWSDOWN as usize;
const PROT_GROWSUP: usize = bindings::rust_mprotect_constant_RUST_MPROTECT_PROT_GROWSUP as usize;
const MM_CP_TRY_CHANGE_WRITABLE: u32 =
    bindings::rust_mprotect_constant_RUST_MPROTECT_MM_CP_TRY_CHANGE_WRITABLE;

fn error_value(error: c_int) -> usize {
    error as usize
}

fn errno(error: u32) -> c_int {
    error as c_int
}

/// Applies protection accounting and page-table changes to one VMA range.
///
/// # Safety
///
/// `state` must be live, its mmap write lock must be held, and `vma` must
/// overlap `[start, end)`. The state must contain flags prepared for this VMA.
unsafe fn fixup(state: *mut State, vma: *mut Vma, start: usize, end: usize) -> c_int {
    // SAFETY: The caller supplies a live state and VMA under the mmap write
    // lock. Helpers perform one projection, predicate, or fixed operation.
    unsafe {
        let mut charged: usize = 0;
        let nrpages = (end.wrapping_sub(start)) >> PAGE_SHIFT;

        bindings::rust_helper_mprotect_load_old_flags(state, vma);

        if bindings::rust_helper_mprotect_vma_sealed(vma) {
            return -errno(bindings::EPERM);
        }

        if bindings::rust_helper_mprotect_same_flags(state) {
            bindings::rust_helper_mprotect_set_prev(state, vma);
            return 0;
        }

        if bindings::rust_helper_mprotect_pfn_check_required(state) {
            let error = bindings::rust_helper_mprotect_pfn_check(state, vma, start, end);
            if error != 0 {
                return error;
            }
        }

        if bindings::rust_helper_mprotect_new_writable(state) {
            if !bindings::rust_helper_mprotect_may_expand(state, true, nrpages)
                && bindings::rust_helper_mprotect_may_expand(state, false, nrpages)
            {
                return -errno(bindings::ENOMEM);
            }

            if !bindings::rust_helper_mprotect_old_charge_exempt(state) {
                if bindings::rust_helper_mprotect_enough_memory(state, nrpages) != 0 {
                    return -errno(bindings::ENOMEM);
                }
                charged = nrpages;
                bindings::rust_helper_mprotect_set_new_account(state);
            }
        } else if bindings::rust_helper_mprotect_old_accounted(state)
            && bindings::rust_helper_mprotect_can_clear_account(vma)
        {
            bindings::rust_helper_mprotect_clear_new_account(state);
        }

        let modified = bindings::rust_helper_mprotect_modify(state, vma, start, end);
        if bindings::rust_helper_mprotect_is_error(modified) {
            if charged != 0 {
                bindings::rust_helper_mprotect_unaccount(charged);
            }
            return bindings::rust_helper_mprotect_pointer_error(modified);
        }

        bindings::rust_helper_mprotect_set_prev(state, modified);
        bindings::rust_helper_mprotect_start_write(modified);
        bindings::rust_helper_mprotect_reset_flags(state, modified);

        let cp_flags = if bindings::rust_helper_mprotect_manual_write_upgrade(modified) {
            MM_CP_TRY_CHANGE_WRITABLE
        } else {
            0
        };
        bindings::rust_helper_mprotect_set_page_prot(modified);
        bindings::rust_helper_mprotect_change_protection(state, modified, start, end, cp_flags);

        if bindings::rust_helper_mprotect_old_accounted(state)
            && !bindings::rust_helper_mprotect_new_accounted(state)
        {
            bindings::rust_helper_mprotect_unaccount(nrpages);
        }

        if bindings::rust_helper_mprotect_new_writable(state)
            && bindings::rust_helper_mprotect_old_locked(state)
            && !bindings::rust_helper_mprotect_old_write(state)
            && !bindings::rust_helper_mprotect_old_shared(state)
        {
            bindings::rust_helper_mprotect_populate(modified, start, end);
        }

        bindings::rust_helper_mprotect_account_old(state, -(nrpages as isize));
        bindings::rust_helper_mprotect_account_new(state, nrpages as isize);
        bindings::rust_helper_mprotect_perf_event(modified);
    }

    0
}

/// Applies prepared protection changes for a kernel-internal caller.
///
/// # Safety
///
/// `state` must be live, its mmap write lock must be held, and `vma` must
/// overlap `[start, end)`. The state must contain flags prepared for this VMA.
#[export]
unsafe extern "C" fn rust_mprotect_fixup(
    state: *mut State,
    vma: *mut Vma,
    start: usize,
    end: usize,
) -> c_int {
    // SAFETY: The C adapter upholds the state, lock, range, and flag
    // requirements.
    unsafe { fixup(state, vma, start, end) }
}

/// Runs the locked production portion of one `mprotect` request.
///
/// # Safety
///
/// `state` must be live and its mmap write lock must be held.
unsafe fn run_locked(
    state: *mut State,
    mut start: usize,
    mut end: usize,
    requested_prot: usize,
    rier: bool,
) -> c_int {
    // SAFETY: The caller holds the mmap write lock and supplies a live state.
    // Each helper performs one narrow access or one existing operation.
    unsafe {
        if !bindings::rust_helper_mprotect_pkey_allowed(state) {
            return -errno(bindings::EINVAL);
        }

        bindings::rust_helper_mprotect_iterator_init(state, start);
        let mut vma = bindings::rust_helper_mprotect_find(state, end);
        if vma.is_null() {
            return -errno(bindings::ENOMEM);
        }

        let mut prot = bindings::rust_helper_mprotect_state_prot(state) as usize;
        let grows = prot & (PROT_GROWSDOWN | PROT_GROWSUP);
        let vma_start = bindings::rust_helper_mprotect_vma_start(vma);
        if grows & PROT_GROWSDOWN != 0 {
            if vma_start >= end {
                return -errno(bindings::ENOMEM);
            }
            start = vma_start;
            if !bindings::rust_helper_mprotect_vma_growsdown(vma) {
                return -errno(bindings::EINVAL);
            }
        } else {
            if vma_start > start {
                return -errno(bindings::ENOMEM);
            }
            if grows & PROT_GROWSUP != 0 {
                end = bindings::rust_helper_mprotect_vma_end(vma);
                if !bindings::rust_helper_mprotect_vma_growsup(vma) {
                    return -errno(bindings::EINVAL);
                }
            }
        }

        bindings::rust_helper_mprotect_set_prev(
            state,
            bindings::rust_helper_mprotect_vma_prev(state),
        );
        if start > vma_start {
            bindings::rust_helper_mprotect_set_prev(state, vma);
        }

        vma = bindings::rust_helper_mprotect_find(state, end);
        if vma.is_null() {
            return -errno(bindings::ENOMEM);
        }
        bindings::rust_helper_mprotect_tlb_gather(state);

        let mut nstart = start;
        let mut tmp = bindings::rust_helper_mprotect_vma_start(vma);
        let mut error = 0;

        while !vma.is_null() {
            if bindings::rust_helper_mprotect_vma_start(vma) != tmp {
                error = -errno(bindings::ENOMEM);
                break;
            }

            if rier && bindings::rust_helper_mprotect_vma_mayexec(vma) {
                prot |= PROT_EXEC;
            }

            bindings::rust_helper_mprotect_prepare_flags(state, vma, prot);

            if bindings::rust_helper_mprotect_access_flags_invalid(state) {
                error = -errno(bindings::EACCES);
                break;
            }
            if bindings::rust_helper_mprotect_deny_write_exec(state, vma) {
                error = -errno(bindings::EACCES);
                break;
            }
            if !bindings::rust_helper_mprotect_validate_flags(state) {
                error = -errno(bindings::EINVAL);
                break;
            }

            error = bindings::rust_helper_mprotect_security_file(vma, requested_prot, prot);
            if error != 0 {
                break;
            }

            tmp = bindings::rust_helper_mprotect_vma_end(vma).min(end);
            if bindings::rust_helper_mprotect_has_vm_op(vma) {
                error = bindings::rust_helper_mprotect_call_vm_op(state, vma, nstart, tmp);
                if error != 0 {
                    break;
                }
            }

            error = fixup(state, vma, nstart, tmp);
            if error != 0 {
                break;
            }

            tmp = bindings::rust_helper_mprotect_iter_end(state);
            nstart = tmp;
            prot = requested_prot;
            vma = bindings::rust_helper_mprotect_find(state, end);
        }

        bindings::rust_helper_mprotect_tlb_finish(state);

        if error == 0 && tmp < end {
            error = -errno(bindings::ENOMEM);
        }
        error
    }
}

/// Validates and executes one `mprotect` or `pkey_mprotect` request.
///
/// # Safety
///
/// The caller must pass a live state for the current address space. This
/// function acquires and releases the mmap write lock.
unsafe fn do_mprotect(state: *mut State) -> usize {
    // SAFETY: The caller supplies a live state for the current address space.
    unsafe {
        let start = bindings::rust_helper_mprotect_untagged_addr(
            bindings::rust_helper_mprotect_state_start(state),
        );

        let mut prot = bindings::rust_helper_mprotect_state_prot(state) as usize;
        let grows = prot & (PROT_GROWSDOWN | PROT_GROWSUP);
        prot &= !(PROT_GROWSDOWN | PROT_GROWSUP);

        if grows == (PROT_GROWSDOWN | PROT_GROWSUP) {
            return error_value(-errno(bindings::EINVAL));
        }
        if start & !PAGE_MASK != 0 {
            return error_value(-errno(bindings::EINVAL));
        }
        let len = bindings::rust_helper_mprotect_state_len(state);
        if len == 0 {
            return 0;
        }

        let end = start.wrapping_add(page_align(len).unwrap_or(0));
        if end <= start {
            return error_value(-errno(bindings::ENOMEM));
        }
        if !bindings::rust_helper_mprotect_validate_prot(prot, start) {
            return error_value(-errno(bindings::EINVAL));
        }

        let requested_prot = prot;
        let rier =
            bindings::rust_helper_mprotect_read_implies_exec() && requested_prot & PROT_READ != 0;

        if bindings::rust_helper_mprotect_write_lock_killable(state) != 0 {
            return error_value(-errno(bindings::EINTR));
        }

        let error = run_locked(state, start, end, requested_prot, rier);
        bindings::rust_helper_mprotect_write_unlock(state);
        error_value(error)
    }
}

/// Validates and executes one `mprotect` or `pkey_mprotect` request.
///
/// # Safety
///
/// The caller must satisfy the requirements of [`do_mprotect`].
#[export]
unsafe extern "C" fn rust_mprotect_do(state: *mut State) -> usize {
    // SAFETY: The C adapter upholds the state and address-space requirements.
    unsafe { do_mprotect(state) }
}
