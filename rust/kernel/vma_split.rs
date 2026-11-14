// SPDX-License-Identifier: GPL-2.0

//! Native orchestration for splitting one VMA.
//!
//! Rust owns the maximum-map-count check, split validation, allocation and
//! preparation ordering, huge-page boundary handling, range updates,
//! structural completion, iterator repositioning, and reverse-order failure
//! rollback. C retains individual field accesses, callbacks, VMA and policy
//! operations, lock primitives, and debug validation.

use crate::bindings;
use core::ffi::c_int;
use core::mem::MaybeUninit;
use macros::export;

type Prepare = bindings::vma_prepare;
type Vma = bindings::vm_area_struct;
type VmaIterator = bindings::vma_iterator;

fn errno(error: u32) -> c_int {
    error as c_int
}

/// Splits one VMA at `addr`.
///
/// # Safety
///
/// The caller must hold the mmap write lock. `vmi` and `vma` must remain live
/// for the complete operation. `addr` must lie strictly inside `vma`.
unsafe fn split(
    vmi: *mut VmaIterator,
    vma: *mut Vma,
    addr: usize,
    new_below: bool,
    check_map_count: bool,
) -> c_int {
    // SAFETY: The caller supplies a live VMA and iterator. Each helper performs
    // one narrow access or one existing primitive operation on those objects.
    unsafe {
        if check_map_count
            && bindings::rust_helper_vma_split_map_count(vma)
                >= bindings::rust_helper_vma_split_max_map_count()
        {
            return -errno(bindings::ENOMEM);
        }

        bindings::rust_helper_vma_split_warn_on_invalid(vma, addr);

        if bindings::rust_helper_vma_split_has_may_split(vma) {
            let err = bindings::rust_helper_vma_split_may_split(vma, addr);
            if err != 0 {
                return err;
            }
        }

        let new = bindings::rust_helper_vma_split_dup(vma);
        if new.is_null() {
            return -errno(bindings::ENOMEM);
        }

        if new_below {
            bindings::rust_helper_vma_split_set_end(new, addr);
        } else {
            bindings::rust_helper_vma_split_set_start(new, addr);
            bindings::rust_helper_vma_split_add_pgoff(new, vma, addr);
        }

        let new_start = bindings::rust_helper_vma_split_vma_start(new);
        let new_end = bindings::rust_helper_vma_split_vma_end(new);
        bindings::rust_helper_vma_split_iter_config(vmi, new_start, new_end);
        if bindings::rust_helper_vma_split_iter_prealloc(vmi, new) != 0 {
            bindings::rust_helper_vma_split_free(new);
            return -errno(bindings::ENOMEM);
        }

        let err = bindings::rust_helper_vma_split_dup_policy(vma, new);
        if err != 0 {
            bindings::rust_helper_vma_split_iter_free(vmi);
            bindings::rust_helper_vma_split_free(new);
            return err;
        }

        let err = bindings::rust_helper_vma_split_anon_clone(new, vma);
        if err != 0 {
            bindings::rust_helper_vma_split_policy_put(new);
            bindings::rust_helper_vma_split_iter_free(vmi);
            bindings::rust_helper_vma_split_free(new);
            return err;
        }

        if bindings::rust_helper_vma_split_has_file(new) {
            bindings::rust_helper_vma_split_get_file(new);
        }
        if bindings::rust_helper_vma_split_has_open(new) {
            bindings::rust_helper_vma_split_open(new);
        }
        bindings::rust_helper_vma_split_start_write(vma);
        bindings::rust_helper_vma_split_start_write(new);

        let mut vp = MaybeUninit::<Prepare>::uninit();
        bindings::rust_helper_vma_split_init_prepare(vp.as_mut_ptr(), vma, new);
        bindings::rust_helper_vma_split_prepare(vp.as_mut_ptr());

        bindings::rust_helper_vma_split_adjust_trans_huge(vma, addr);
        if bindings::rust_helper_vma_split_is_hugetlb(vma) {
            bindings::rust_helper_vma_split_hugetlb(vma, addr);
        }

        if new_below {
            bindings::rust_helper_vma_split_set_start(vma, addr);
            bindings::rust_helper_vma_split_add_pgoff(vma, new, addr);
        } else {
            bindings::rust_helper_vma_split_set_end(vma, addr);
        }

        bindings::rust_helper_vma_split_complete(vp.as_mut_ptr(), vmi, vma);
        bindings::rust_helper_vma_split_validate_mm(vma);

        if new_below {
            bindings::rust_helper_vma_split_next(vmi);
        } else {
            bindings::rust_helper_vma_split_prev(vmi);
        }

        0
    }
}

/// Splits one VMA at `addr`.
///
/// # Safety
///
/// The caller must hold the mmap write lock. `vmi` and `vma` must remain live
/// for the complete operation. `addr` must lie strictly inside `vma`.
#[export]
unsafe extern "C" fn rust_vma_split(
    vmi: *mut VmaIterator,
    vma: *mut Vma,
    addr: usize,
    new_below: c_int,
    check_map_count: bool,
) -> c_int {
    // SAFETY: The caller upholds the split-operation requirements.
    unsafe { split(vmi, vma, addr, new_below != 0, check_map_count) }
}
