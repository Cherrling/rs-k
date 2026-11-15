// SPDX-License-Identifier: GPL-2.0

//! Native region unmapping and VMA release.
//!
//! Rust owns the TLB gather, high-water, VMA unmap, iterator reset,
//! page-table free, and TLB finish order. Rust also owns the close callback,
//! file, policy, and final VMA release order. C exposes one existing operation
//! or one indivisible callback transition per helper.

use crate::bindings;
use macros::export;

type Tlb = bindings::mmu_gather;
type Unmap = bindings::unmap_desc;
type Vma = bindings::vm_area_struct;

/// Closes and releases one detached VMA.
///
/// # Safety
///
/// `vma` must be live, detached from every address-space tree, and must not
/// have remaining references. The caller must be in sleepable context and
/// must permit the close callback to run.
unsafe fn remove(vma: *mut Vma) {
    // SAFETY: The caller permits sleeping before the close callback.
    unsafe { bindings::rust_helper_vma_region_might_sleep() };

    // SAFETY: The VMA is detached and cannot gain new callbacks.
    if unsafe { bindings::rust_helper_vma_region_needs_close(vma) } {
        // SAFETY: The close callback runs once and installs dummy operations.
        unsafe { bindings::rust_helper_vma_region_close(vma) };
    }

    // SAFETY: The VMA owns the file reference that is released here.
    let file = unsafe { bindings::rust_helper_vma_region_file(vma) };
    if !file.is_null() {
        // SAFETY: The file reference belongs to this VMA.
        unsafe { bindings::rust_helper_vma_region_fput(file) };
    }

    // SAFETY: The memory-policy reference belongs to this detached VMA.
    unsafe { bindings::rust_helper_vma_region_policy_put(vma) };
    // SAFETY: No operation after the free uses the VMA.
    unsafe { bindings::rust_helper_vma_region_free_vma(vma) };
}

/// Removes page-table information for one VMA region.
///
/// # Safety
///
/// `unmap` must describe live VMAs under the caller's mmap lock. `tlb` must
/// point to uninitialized storage private to this call. No other operation may
/// use that TLB storage until this function returns.
unsafe fn unmap_region(unmap: *mut Unmap, tlb: *mut Tlb) {
    // SAFETY: The unmap descriptor contains a live first VMA.
    let mm = unsafe { bindings::rust_helper_vma_region_unmap_mm(unmap) };

    // SAFETY: The private TLB storage is uninitialized for this address space.
    unsafe { bindings::rust_helper_vma_region_tlb_gather(tlb, mm) };
    // SAFETY: The mmap lock keeps the high-water fields stable.
    unsafe { bindings::rust_helper_vma_region_update_hiwater(mm) };
    // SAFETY: The descriptor describes the live VMA range to unmap.
    unsafe { bindings::rust_helper_vma_region_unmap_vmas(tlb, unmap) };
    // SAFETY: The page-table walk resumes at the recorded reset point.
    unsafe { bindings::rust_helper_vma_region_iterator_reset(unmap) };
    // SAFETY: All VMAs in the described range have been unmapped.
    unsafe { bindings::rust_helper_vma_region_free_pgtables(tlb, unmap) };
    // SAFETY: Page-table removal completed and the TLB state is active.
    unsafe { bindings::rust_helper_vma_region_tlb_finish(tlb) };
}

/// Closes and releases one detached VMA.
///
/// # Safety
///
/// The requirements of [`remove`] apply.
#[export]
unsafe extern "C" fn rust_vma_region_remove(vma: *mut Vma) {
    // SAFETY: The caller upholds the VMA release requirements.
    unsafe { remove(vma) }
}

/// Removes page-table information for one VMA region.
///
/// # Safety
///
/// The requirements of [`unmap_region`] apply. The C adapter allocates `tlb`
/// on its stack and transfers it to Rust for the function duration.
#[export]
unsafe extern "C" fn rust_vma_region_unmap(unmap: *mut Unmap, tlb: *mut Tlb) {
    // SAFETY: The caller upholds the region unmap requirements.
    unsafe { unmap_region(unmap, tlb) }
}
