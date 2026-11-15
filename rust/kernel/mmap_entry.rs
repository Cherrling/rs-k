// SPDX-License-Identifier: GPL-2.0

//! Native orchestration for creating user memory mappings.
//!
//! Rust owns request validation, address selection, file and anonymous mapping
//! policy, VMA flag policy, reservation handling, region creation, and
//! population decisions. C retains field projections and existing operations.

use crate::bindings;
use macros::export;

type State = bindings::rust_mmap_entry_state;

const PROT_READ: usize = bindings::rust_mmap_entry_constant_RUST_MMAP_ENTRY_PROT_READ as usize;
const PROT_EXEC: usize = bindings::rust_mmap_entry_constant_RUST_MMAP_ENTRY_PROT_EXEC as usize;
const PROT_WRITE: usize = bindings::rust_mmap_entry_constant_RUST_MMAP_ENTRY_PROT_WRITE as usize;
const MAP_TYPE: usize = bindings::rust_mmap_entry_constant_RUST_MMAP_ENTRY_MAP_TYPE as usize;
const MAP_FIXED: usize = bindings::rust_mmap_entry_constant_RUST_MMAP_ENTRY_MAP_FIXED as usize;
const MAP_FIXED_NOREPLACE: usize =
    bindings::rust_mmap_entry_constant_RUST_MMAP_ENTRY_MAP_FIXED_NOREPLACE as usize;
const MAP_SHARED: usize = bindings::rust_mmap_entry_constant_RUST_MMAP_ENTRY_MAP_SHARED as usize;
const MAP_SHARED_VALIDATE: usize =
    bindings::rust_mmap_entry_constant_RUST_MMAP_ENTRY_MAP_SHARED_VALIDATE as usize;
const MAP_PRIVATE: usize = bindings::rust_mmap_entry_constant_RUST_MMAP_ENTRY_MAP_PRIVATE as usize;
const MAP_DROPPABLE: usize =
    bindings::rust_mmap_entry_constant_RUST_MMAP_ENTRY_MAP_DROPPABLE as usize;
const MAP_LOCKED: usize = bindings::rust_mmap_entry_constant_RUST_MMAP_ENTRY_MAP_LOCKED as usize;
const MAP_HUGETLB: usize = bindings::rust_mmap_entry_constant_RUST_MMAP_ENTRY_MAP_HUGETLB as usize;
const MAP_POPULATE: usize =
    bindings::rust_mmap_entry_constant_RUST_MMAP_ENTRY_MAP_POPULATE as usize;
const MAP_NONBLOCK: usize =
    bindings::rust_mmap_entry_constant_RUST_MMAP_ENTRY_MAP_NONBLOCK as usize;
const MAP_NORESERVE: usize =
    bindings::rust_mmap_entry_constant_RUST_MMAP_ENTRY_MAP_NORESERVE as usize;

const FILE_READ: u32 = bindings::rust_mmap_entry_file_fact_RUST_MMAP_ENTRY_FILE_READ;
const FILE_WRITE: u32 = bindings::rust_mmap_entry_file_fact_RUST_MMAP_ENTRY_FILE_WRITE;
const FILE_SWAPFILE: u32 = bindings::rust_mmap_entry_file_fact_RUST_MMAP_ENTRY_FILE_SWAPFILE;
const FILE_APPEND: u32 = bindings::rust_mmap_entry_file_fact_RUST_MMAP_ENTRY_FILE_APPEND;
const FILE_NOEXEC: u32 = bindings::rust_mmap_entry_file_fact_RUST_MMAP_ENTRY_FILE_NOEXEC;
const FILE_HUGEPAGES: u32 = bindings::rust_mmap_entry_file_fact_RUST_MMAP_ENTRY_FILE_HUGEPAGES;
const FILE_MMAP_SYNC: u32 = bindings::rust_mmap_entry_file_fact_RUST_MMAP_ENTRY_FILE_MMAP_SYNC;

fn error_value(error: i32) -> usize {
    error as usize
}

fn errno(error: u32) -> i32 {
    error as i32
}

/// Applies the checks that precede unmapped-area selection.
///
/// # Safety
///
/// The caller supplies a live mmap entry state.
unsafe fn validate_request(state: *mut State) -> Option<usize> {
    // SAFETY: The caller supplies a live mmap entry state.
    unsafe {
        bindings::rust_helper_mmap_entry_clear_populate(state);
        bindings::rust_helper_mmap_entry_assert_write_locked(state);

        if bindings::rust_helper_mmap_entry_len(state) == 0 {
            return Some(error_value(-errno(bindings::EINVAL)));
        }

        let prot = bindings::rust_helper_mmap_entry_prot(state);
        if prot & PROT_READ != 0 && bindings::rust_helper_mmap_entry_read_implies_exec() {
            let has_file = bindings::rust_helper_mmap_entry_has_file(state);
            let noexec =
                has_file && bindings::rust_helper_mmap_entry_file_facts(state) & FILE_NOEXEC != 0;
            if !noexec {
                bindings::rust_helper_mmap_entry_add_prot_exec(state);
            }
        }

        if bindings::rust_helper_mmap_entry_flags(state) & MAP_FIXED_NOREPLACE != 0 {
            bindings::rust_helper_mmap_entry_add_map_fixed(state);
        }
        let flags = bindings::rust_helper_mmap_entry_flags(state);
        if flags & MAP_FIXED == 0 {
            bindings::rust_helper_mmap_entry_round_hint(state);
        }

        bindings::rust_helper_mmap_entry_align_len(state);
        if bindings::rust_helper_mmap_entry_len(state) == 0 {
            return Some(error_value(-errno(bindings::ENOMEM)));
        }

        if bindings::rust_helper_mmap_entry_pgoff_overflows(state) {
            return Some(error_value(-errno(bindings::EOVERFLOW)));
        }

        if bindings::rust_helper_mmap_entry_map_count_exceeded(state) {
            return Some(error_value(-errno(bindings::ENOMEM)));
        }

        None
    }
}

/// Applies the common file-private checks after shared checks.
///
/// # Safety
///
/// The caller supplies a live mmap entry state with a file.
unsafe fn check_file_private(state: *mut State, file_facts: u32) -> Option<usize> {
    // SAFETY: The caller supplies a live mmap entry state with a file.
    unsafe {
        if file_facts & FILE_READ == 0 {
            return Some(error_value(-errno(bindings::EACCES)));
        }

        if file_facts & FILE_NOEXEC != 0 {
            if bindings::rust_helper_mmap_entry_vma_exec(state) {
                return Some(error_value(-errno(bindings::EPERM)));
            }
            bindings::rust_helper_mmap_entry_clear_may_exec(state);
        }

        if !bindings::rust_helper_mmap_entry_can_mmap_file(state) {
            return Some(error_value(-errno(bindings::ENODEV)));
        }

        if bindings::rust_helper_mmap_entry_vma_can_grow(state) {
            return Some(error_value(-errno(bindings::EINVAL)));
        }

        None
    }
}

/// Applies file-backed mapping policy.
///
/// # Safety
///
/// The caller supplies a live mmap entry state with a file.
unsafe fn apply_file_mapping(state: *mut State) -> Option<usize> {
    // SAFETY: The caller supplies a live mmap entry state with a file.
    unsafe {
        if !bindings::rust_helper_mmap_entry_file_mmap_ok(state) {
            return Some(error_value(-errno(bindings::EOVERFLOW)));
        }

        bindings::rust_helper_mmap_entry_init_file_flags_mask(state);
        let file_facts = bindings::rust_helper_mmap_entry_file_facts(state);
        if file_facts & FILE_MMAP_SYNC != 0 {
            bindings::rust_helper_mmap_entry_add_mmap_sync_flags_mask(state);
        }

        let map_type = bindings::rust_helper_mmap_entry_flags(state) & MAP_TYPE;
        if map_type == MAP_SHARED {
            bindings::rust_helper_mmap_entry_strip_legacy_flags(state);
        }

        if map_type == MAP_SHARED || map_type == MAP_SHARED_VALIDATE {
            if bindings::rust_helper_mmap_entry_file_flags_unsupported(state) {
                return Some(error_value(-errno(bindings::EOPNOTSUPP)));
            }

            let prot = bindings::rust_helper_mmap_entry_prot(state);
            if prot & PROT_WRITE != 0 {
                if file_facts & FILE_WRITE == 0 {
                    return Some(error_value(-errno(bindings::EACCES)));
                }
                if file_facts & FILE_SWAPFILE != 0 {
                    return Some(error_value(-errno(bindings::ETXTBSY)));
                }
            }

            if file_facts & FILE_APPEND != 0 && file_facts & FILE_WRITE != 0 {
                return Some(error_value(-errno(bindings::EACCES)));
            }

            bindings::rust_helper_mmap_entry_set_shared_vma_flags(state);
            if file_facts & FILE_WRITE == 0 {
                bindings::rust_helper_mmap_entry_clear_shared_write_vma_flags(state);
            }
        } else if map_type != MAP_PRIVATE {
            return Some(error_value(-errno(bindings::EINVAL)));
        }

        check_file_private(state, file_facts)
    }
}

/// Applies anonymous mapping policy.
///
/// # Safety
///
/// The caller supplies a live mmap entry state without a file.
unsafe fn apply_anonymous_mapping(state: *mut State) -> Option<usize> {
    // SAFETY: The caller supplies a live mmap entry state without a file.
    unsafe {
        let flags = bindings::rust_helper_mmap_entry_flags(state);
        match flags & MAP_TYPE {
            MAP_SHARED => {
                if bindings::rust_helper_mmap_entry_vma_can_grow(state) {
                    return Some(error_value(-errno(bindings::EINVAL)));
                }
                bindings::rust_helper_mmap_entry_set_anon_pgoff_zero(state);
                bindings::rust_helper_mmap_entry_set_shared_vma_flags(state);
            }
            MAP_DROPPABLE => {
                if !bindings::rust_helper_mmap_entry_vma_droppable_supported() {
                    return Some(error_value(-errno(bindings::EOPNOTSUPP)));
                }
                bindings::rust_helper_mmap_entry_set_droppable_flag(state);
                if flags & (MAP_LOCKED | MAP_HUGETLB) != 0 {
                    return Some(error_value(-errno(bindings::EINVAL)));
                }
                if bindings::rust_helper_mmap_entry_vma_can_grow(state) {
                    return Some(error_value(-errno(bindings::EINVAL)));
                }
                bindings::rust_helper_mmap_entry_set_droppable_lifetime_flags(state);
                bindings::rust_helper_mmap_entry_set_anon_pgoff(state);
            }
            MAP_PRIVATE => bindings::rust_helper_mmap_entry_set_anon_pgoff(state),
            _ => return Some(error_value(-errno(bindings::EINVAL))),
        }
        None
    }
}

/// Creates one user memory mapping.
///
/// # Safety
///
/// The caller must hold the mmap write lock. The state, file, VMAs, and output
/// pointers must remain valid until this function returns.
unsafe fn do_mmap(state: *mut State) -> usize {
    // SAFETY: The caller supplies a live mmap entry state. Each helper performs
    // one narrow projection, predicate, or existing operation.
    unsafe {
        if let Some(error) = validate_request(state) {
            return error;
        }

        let mut pkey = 0;
        if bindings::rust_helper_mmap_entry_prot(state) == PROT_EXEC {
            let candidate = bindings::rust_helper_mmap_entry_execute_only_pkey(state);
            if candidate >= 0 {
                pkey = candidate as i32;
            }
        }
        bindings::rust_helper_mmap_entry_prepare_vma_flags(state, pkey);

        bindings::rust_helper_mmap_entry_get_unmapped_area(state);
        if bindings::rust_helper_mmap_entry_addr_is_error(state) {
            return bindings::rust_helper_mmap_entry_addr(state);
        }

        let flags = bindings::rust_helper_mmap_entry_flags(state);
        if flags & MAP_FIXED_NOREPLACE != 0
            && bindings::rust_helper_mmap_entry_fixed_noreplace_intersects(state)
        {
            return error_value(-errno(bindings::EEXIST));
        }

        if flags & MAP_LOCKED != 0 && !bindings::rust_helper_mmap_entry_can_do_mlock() {
            return error_value(-errno(bindings::EPERM));
        }

        if !bindings::rust_helper_mmap_entry_mlock_future_ok(state) {
            return error_value(-errno(bindings::EAGAIN));
        }

        let has_file = bindings::rust_helper_mmap_entry_has_file(state);
        if has_file {
            if let Some(error) = apply_file_mapping(state) {
                return error;
            }

            let error = bindings::rust_helper_mmap_entry_check_memfd_seals(state);
            if error != 0 {
                return error as usize;
            }
        } else if let Some(error) = apply_anonymous_mapping(state) {
            return error;
        }

        let flags = bindings::rust_helper_mmap_entry_flags(state);
        if flags & MAP_NORESERVE != 0 {
            if bindings::rust_helper_mmap_entry_overcommit_not_never() {
                bindings::rust_helper_mmap_entry_set_noreserve(state);
            }
            if has_file && bindings::rust_helper_mmap_entry_file_facts(state) & FILE_HUGEPAGES != 0
            {
                bindings::rust_helper_mmap_entry_set_noreserve(state);
            }
        }

        bindings::rust_helper_mmap_entry_mmap_region(state);
        if bindings::rust_helper_mmap_entry_addr_is_error(state) {
            return bindings::rust_helper_mmap_entry_addr(state);
        }

        let flags = bindings::rust_helper_mmap_entry_flags(state);
        let populate = bindings::rust_helper_mmap_entry_vma_locked(state)
            || (flags & MAP_POPULATE != 0 && flags & MAP_NONBLOCK == 0);
        if populate {
            bindings::rust_helper_mmap_entry_set_populate(state);
        }

        bindings::rust_helper_mmap_entry_addr(state)
    }
}

/// Creates one user memory mapping.
///
/// # Safety
///
/// The caller must satisfy the requirements of [`do_mmap`].
#[export]
unsafe extern "C" fn rust_mmap_entry(state: *mut State) -> usize {
    // SAFETY: The caller upholds the mmap entry requirements.
    unsafe { do_mmap(state) }
}
