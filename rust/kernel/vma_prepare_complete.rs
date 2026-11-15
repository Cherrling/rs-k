// SPDX-License-Identifier: GPL-2.0

//! Native initialization, locking, and completion for VMA structural updates.
//!
//! Rust owns preparation-object composition, removal-slot selection, the
//! paired lock-acquisition and lock-release order, reverse-map updates,
//! uprobe transitions, structural iterator storage, detached-VMA removal,
//! accounting, policy release, and VMA free order. C retains the individual
//! lock, map-tree, callback, and lifecycle primitives.

use crate::bindings;
use core::ptr::{addr_of_mut, null_mut};
use macros::export;

type Mm = bindings::mm_struct;
type Prepare = bindings::vma_prepare;
type VmaIterator = bindings::vma_iterator;

/// Initializes the VMA preparation object.
///
/// # Safety
///
/// `vp` must point to writable storage. `vma` must remain live and `vmg` must
/// either be null or point to a live merge operation.
unsafe fn init_prepare(
    vp: *mut Prepare,
    vma: *mut bindings::vm_area_struct,
    vmg: *mut bindings::vma_merge_struct,
) {
    // SAFETY: The caller supplies writable preparation storage and live input
    // objects. Zero initializes every optional pointer and the skip bit.
    unsafe {
        core::ptr::write_bytes(vp, 0, 1);
        (*vp).vma = vma;
        (*vp).anon_vma = (*vma).anon_vma;

        if !vmg.is_null() {
            let mut remove = addr_of_mut!((*vp).remove);
            if bindings::rust_helper_vma_init_prepare_remove_middle(vmg) {
                *remove = bindings::rust_helper_vma_init_prepare_middle(vmg);
                remove = addr_of_mut!((*vp).remove2);
            }
            if bindings::rust_helper_vma_init_prepare_remove_next(vmg) {
                *remove = bindings::rust_helper_vma_init_prepare_next(vmg);
            }

            let adjust = if bindings::rust_helper_vma_init_prepare_adjust_middle(vmg) {
                bindings::rust_helper_vma_init_prepare_middle(vmg)
            } else if bindings::rust_helper_vma_init_prepare_adjust_next(vmg) {
                bindings::rust_helper_vma_init_prepare_next(vmg)
            } else {
                null_mut()
            };
            (*vp).adj_next = adjust;

            if (*vp).anon_vma.is_null() && !adjust.is_null() {
                (*vp).anon_vma = (*adjust).anon_vma;
            }
        }

        let file = bindings::rust_helper_vma_init_prepare_vma_file(vma);
        (*vp).file = file;
        if !file.is_null() {
            (*vp).mapping = bindings::rust_helper_vma_init_prepare_vma_mapping(vma);
        }

        bindings::rust_helper_vma_init_prepare_warn_anon(vp);
        if !vmg.is_null() && bindings::rust_helper_vma_init_prepare_skip_uprobe(vmg) {
            (*vp).set_skip_vma_uprobe(true);
        }
    }
}

/// Locks and removes the reverse-map state for a VMA update.
///
/// # Safety
///
/// The caller must hold the mmap write lock. `vp` must contain live,
/// initialized pointers for the complete operation.
unsafe fn prepare(vp: *mut Prepare) {
    // SAFETY: The caller supplies an initialized preparation object. Each
    // helper performs one existing locking or reverse-map primitive.
    unsafe {
        let vma = (*vp).vma;
        let adj_next = (*vp).adj_next;
        let file = (*vp).file;
        let mapping = (*vp).mapping;
        let anon_vma = (*vp).anon_vma;

        if !file.is_null() {
            bindings::rust_helper_vma_prepare_uprobe_munmap(
                vma,
                bindings::rust_helper_vma_prepare_vma_start(vma),
                bindings::rust_helper_vma_prepare_vma_end(vma),
            );

            if !adj_next.is_null() {
                bindings::rust_helper_vma_prepare_uprobe_munmap(
                    adj_next,
                    bindings::rust_helper_vma_prepare_vma_start(adj_next),
                    bindings::rust_helper_vma_prepare_vma_end(adj_next),
                );
            }

            bindings::rust_helper_vma_prepare_immap_lock_write(mapping);
            let insert = (*vp).insert;
            if !insert.is_null() {
                let insert_file = bindings::rust_helper_vma_init_prepare_vma_file(insert);
                if !insert_file.is_null() {
                    bindings::rust_helper_vma_prepare_link_file(
                        insert,
                        bindings::rust_helper_vma_init_prepare_vma_mapping(insert),
                    );
                }
            }
        }

        if !anon_vma.is_null() {
            bindings::rust_helper_vma_prepare_anon_lock_write(anon_vma);
            bindings::rust_helper_vma_prepare_anon_rmap_pre_update(vma);
            if !adj_next.is_null() {
                bindings::rust_helper_vma_prepare_anon_rmap_pre_update(adj_next);
            }
        }

        if !file.is_null() {
            bindings::rust_helper_vma_prepare_dcache_lock(mapping);
            bindings::rust_helper_vma_prepare_mapping_remove(vma, mapping);
            if !adj_next.is_null() {
                bindings::rust_helper_vma_prepare_mapping_remove(adj_next, mapping);
            }
        }
    }
}

/// Reattaches reverse-map state and releases a VMA preparation object.
///
/// # Safety
///
/// `vp` must be live and prepared by [`rust_vma_prepare`]. `vmi` and `mm`
/// must remain live until the operation completes.
unsafe fn complete(vp: *mut Prepare, vmi: *mut VmaIterator, mm: *mut Mm) {
    // SAFETY: The caller supplies a prepared object and live iterator and
    // address space. Helpers perform one existing primitive per call.
    unsafe {
        let vma = (*vp).vma;
        let adj_next = (*vp).adj_next;
        let file = (*vp).file;
        let mapping = (*vp).mapping;
        let anon_vma = (*vp).anon_vma;
        let insert = (*vp).insert;
        let mut remove = (*vp).remove;

        if !file.is_null() {
            if !adj_next.is_null() {
                bindings::rust_helper_vma_prepare_mapping_insert(adj_next, mapping);
            }
            bindings::rust_helper_vma_prepare_mapping_insert(vma, mapping);
            bindings::rust_helper_vma_prepare_dcache_unlock(mapping);
        }

        if !remove.is_null() && !file.is_null() {
            bindings::rust_helper_vma_prepare_remove_shared(remove, mapping);
            let remove2 = (*vp).remove2;
            if !remove2.is_null() {
                bindings::rust_helper_vma_prepare_remove_shared(remove2, mapping);
            }
        } else if !insert.is_null() {
            bindings::rust_helper_vma_prepare_iter_store(vmi, insert);
            bindings::rust_helper_vma_prepare_inc_map_count(mm);
        }

        if !anon_vma.is_null() {
            bindings::rust_helper_vma_prepare_anon_rmap_post_update(vma);
            if !adj_next.is_null() {
                bindings::rust_helper_vma_prepare_anon_rmap_post_update(adj_next);
            }
            bindings::rust_helper_vma_prepare_anon_unlock_write(anon_vma);
        }

        if !file.is_null() {
            bindings::rust_helper_vma_prepare_immap_unlock_write(mapping);
            if !(*vp).skip_vma_uprobe() {
                bindings::rust_helper_vma_prepare_uprobe_mmap(vma);
                if !adj_next.is_null() {
                    bindings::rust_helper_vma_prepare_uprobe_mmap(adj_next);
                }
            }
        }

        while !remove.is_null() {
            bindings::rust_helper_vma_prepare_mark_detached(remove);
            if !file.is_null() {
                bindings::rust_helper_vma_prepare_uprobe_munmap(
                    remove,
                    bindings::rust_helper_vma_prepare_vma_start(remove),
                    bindings::rust_helper_vma_prepare_vma_end(remove),
                );
                bindings::rust_helper_vma_prepare_fput(file);
            }
            if !(*remove).anon_vma.is_null() {
                bindings::rust_helper_vma_prepare_unlink_anon(remove);
            }
            bindings::rust_helper_vma_prepare_dec_map_count(mm);
            bindings::rust_helper_vma_prepare_policy_put(remove);
            if (*vp).remove2.is_null() {
                bindings::rust_helper_vma_prepare_warn_remove_end((*vp).vma, remove);
            }
            bindings::rust_helper_vma_prepare_free_vma(remove);

            let remove2 = (*vp).remove2;
            if remove2.is_null() {
                break;
            }
            (*vp).remove = remove2;
            (*vp).remove2 = null_mut();
            remove = remove2;
        }

        if !insert.is_null() && !file.is_null() {
            bindings::rust_helper_vma_prepare_uprobe_mmap(insert);
        }
    }
}

/// Initializes a VMA preparation object.
///
/// # Safety
///
/// `vp` must point to writable storage. `vma` must remain live and `vmg` must
/// either be null or point to a live merge operation.
#[export]
unsafe extern "C" fn rust_vma_init_prepare(
    vp: *mut Prepare,
    vma: *mut bindings::vm_area_struct,
    vmg: *mut bindings::vma_merge_struct,
) {
    // SAFETY: The caller upholds the preparation initialization requirements.
    unsafe { init_prepare(vp, vma, vmg) }
}

/// Locks and removes VMA reverse-map state.
///
/// # Safety
///
/// The caller must hold the mmap write lock and supply a live initialized
/// preparation object.
#[export]
unsafe extern "C" fn rust_vma_prepare(vp: *mut Prepare) {
    // SAFETY: The caller upholds the VMA preparation requirements.
    unsafe { prepare(vp) }
}

/// Reattaches VMA state and releases VMA preparation resources.
///
/// # Safety
///
/// The caller must supply a live prepared object, iterator, and address space.
#[export]
unsafe extern "C" fn rust_vma_complete(vp: *mut Prepare, vmi: *mut VmaIterator, mm: *mut Mm) {
    // SAFETY: The caller upholds the VMA completion requirements.
    unsafe { complete(vp, vmi, mm) }
}
