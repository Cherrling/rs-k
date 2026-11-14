// SPDX-License-Identifier: GPL-2.0

//! Native orchestration for shrinking the end of one VMA.
//!
//! Rust owns the iterator configuration, preallocation failure check, VMA lock
//! ordering, preparation, huge-page adjustment, range update, completion, and
//! validation order. C retains the individual iterator, locking, preparation,
//! range, and debug primitives.

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

/// Shrinks the end of one VMA.
///
/// # Safety
///
/// The caller must hold the mmap write lock. `vmi` and `vma` must remain live
/// for the complete operation. `end` must not extend beyond the current end.
unsafe fn shrink(vmi: *mut VmaIterator, vma: *mut Vma, end: usize) -> c_int {
    // SAFETY: The caller supplies a live VMA and iterator. Each helper performs
    // one narrow access or one existing primitive operation on those objects.
    unsafe {
        bindings::rust_helper_vma_shrink_validate(vma, end);

        let old_end = bindings::rust_helper_vma_shrink_vma_end(vma);
        bindings::rust_helper_vma_shrink_iter_config(vmi, end, old_end);
        if bindings::rust_helper_vma_shrink_iter_prealloc(vmi) != 0 {
            return -errno(bindings::ENOMEM);
        }

        bindings::rust_helper_vma_shrink_start_write(vma);

        let mut vp = MaybeUninit::<Prepare>::uninit();
        bindings::rust_helper_vma_shrink_init_prepare(vp.as_mut_ptr(), vma);
        bindings::rust_helper_vma_shrink_prepare(vp.as_mut_ptr());
        bindings::rust_helper_vma_shrink_adjust_trans_huge(vma, end);

        bindings::rust_helper_vma_shrink_iter_clear(vmi);
        bindings::rust_helper_vma_shrink_set_end(vma, end);
        bindings::rust_helper_vma_shrink_complete(vp.as_mut_ptr(), vmi, vma);
        bindings::rust_helper_vma_shrink_validate_mm(vma);

        0
    }
}

/// Shrinks the end of one VMA.
///
/// # Safety
///
/// The caller must hold the mmap write lock and supply a live VMA and iterator.
#[export]
unsafe extern "C" fn rust_vma_shrink(vmi: *mut VmaIterator, vma: *mut Vma, end: usize) -> c_int {
    // SAFETY: The caller upholds the VMA shrink requirements.
    unsafe { shrink(vmi, vma, end) }
}
