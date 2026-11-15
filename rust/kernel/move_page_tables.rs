// SPDX-License-Identifier: GPL-2.0

//! Native orchestration for moving page-table ranges.
//!
//! Rust owns range termination, extent calculation, page-table level selection,
//! transparent-huge-page fallback, PTE retry handling, failure exits, and
//! moved-byte accounting. C retains page-table lookup, allocation, architecture
//! updates, TLB and cache maintenance, and notifier operations.

use crate::bindings;
use macros::export;

type State = bindings::rust_move_page_tables_state;

const NORMAL_PMD: u32 = bindings::pgt_entry_NORMAL_PMD;
const HPAGE_PMD: u32 = bindings::pgt_entry_HPAGE_PMD;
const NORMAL_PUD: u32 = bindings::pgt_entry_NORMAL_PUD;
const HPAGE_PUD: u32 = bindings::pgt_entry_HPAGE_PUD;

/// Returns the largest extent that is aligned in both address ranges.
///
/// # Safety
///
/// The caller supplies a live page-table move state.
unsafe fn get_extent(state: *mut State, entry: u32) -> usize {
    // SAFETY: The caller supplies a live page-table move state.
    unsafe {
        let (mask, size) = match entry {
            NORMAL_PMD | HPAGE_PMD => (
                bindings::rust_helper_move_page_tables_pmd_mask(),
                bindings::rust_helper_move_page_tables_pmd_size(),
            ),
            NORMAL_PUD | HPAGE_PUD => (
                bindings::rust_helper_move_page_tables_pud_mask(),
                bindings::rust_helper_move_page_tables_pud_size(),
            ),
            _ => {
                bindings::rust_helper_move_page_tables_warn_invalid();
                return 0;
            }
        };

        let old_addr = bindings::rust_helper_move_page_tables_old_addr(state);
        let old_end = bindings::rust_helper_move_page_tables_old_end(state);
        let new_addr = bindings::rust_helper_move_page_tables_new_addr(state);
        let next = old_addr.wrapping_add(size) & mask;
        let mut extent = next.wrapping_sub(old_addr);

        extent = extent.min(old_end.wrapping_sub(old_addr));
        let next = new_addr.wrapping_add(size) & mask;
        extent.min(next.wrapping_sub(new_addr))
    }
}

/// Moves all reachable page-table entries in one page-table move range.
///
/// # Safety
///
/// The caller must hold the mmap write lock. The move state and both VMAs must
/// remain valid until this function returns.
unsafe fn move_page_tables(state: *mut State) -> usize {
    // SAFETY: The caller supplies a live page-table move state.
    unsafe {
        if bindings::rust_helper_move_page_tables_len(state) == 0 {
            return 0;
        }

        if bindings::rust_helper_move_page_tables_is_hugetlb(state) {
            return bindings::rust_helper_move_page_tables_move_hugetlb(state);
        }

        bindings::rust_helper_move_page_tables_realign_pmd(state);
        bindings::rust_helper_move_page_tables_start(state);

        'outer: while !bindings::rust_helper_move_page_tables_done(state) {
            bindings::rust_helper_move_page_tables_cond_resched();

            let mut extent = get_extent(state, NORMAL_PUD);
            if bindings::rust_helper_move_page_tables_get_old_pud(state) {
                if !bindings::rust_helper_move_page_tables_alloc_new_pud(state) {
                    break 'outer;
                }

                if bindings::rust_helper_move_page_tables_pud_trans_huge(state) {
                    if extent == bindings::rust_helper_move_page_tables_hpage_pud_size() {
                        bindings::rust_helper_move_page_tables_move_entry(state, HPAGE_PUD);
                        bindings::rust_helper_move_page_tables_next(state, extent);
                        continue 'outer;
                    }
                } else if bindings::rust_helper_move_page_tables_can_move_pud()
                    && extent == bindings::rust_helper_move_page_tables_pud_size()
                    && bindings::rust_helper_move_page_tables_move_entry(state, NORMAL_PUD)
                {
                    bindings::rust_helper_move_page_tables_next(state, extent);
                    continue 'outer;
                }

                extent = get_extent(state, NORMAL_PMD);
                if bindings::rust_helper_move_page_tables_get_old_pmd(state) {
                    if !bindings::rust_helper_move_page_tables_alloc_new_pmd(state) {
                        break 'outer;
                    }

                    'again: loop {
                        if bindings::rust_helper_move_page_tables_pmd_is_huge(state) {
                            if extent == bindings::rust_helper_move_page_tables_hpage_pmd_size()
                                && bindings::rust_helper_move_page_tables_move_entry(
                                    state, HPAGE_PMD,
                                )
                            {
                                bindings::rust_helper_move_page_tables_next(state, extent);
                                continue 'outer;
                            }
                            bindings::rust_helper_move_page_tables_split_huge_pmd(state);
                        } else if bindings::rust_helper_move_page_tables_can_move_pmd()
                            && extent == bindings::rust_helper_move_page_tables_pmd_size()
                            && bindings::rust_helper_move_page_tables_move_entry(state, NORMAL_PMD)
                        {
                            bindings::rust_helper_move_page_tables_next(state, extent);
                            continue 'outer;
                        }

                        if bindings::rust_helper_move_page_tables_pmd_none(state) {
                            bindings::rust_helper_move_page_tables_next(state, extent);
                            continue 'outer;
                        }
                        if bindings::rust_helper_move_page_tables_pte_alloc(state) {
                            break 'outer;
                        }
                        if bindings::rust_helper_move_page_tables_move_ptes(state, extent) < 0 {
                            continue 'again;
                        }
                        break 'again;
                    }
                }
            }

            bindings::rust_helper_move_page_tables_next(state, extent);
        }

        bindings::rust_helper_move_page_tables_end(state);

        let old_addr = bindings::rust_helper_move_page_tables_old_addr(state);
        let original_old_addr = bindings::rust_helper_move_page_tables_old_end(state)
            .wrapping_sub(bindings::rust_helper_move_page_tables_len(state));

        old_addr.saturating_sub(original_old_addr)
    }
}

/// Moves all reachable page-table entries in one page-table move range.
///
/// # Safety
///
/// The caller must satisfy the requirements of [`move_page_tables`].
#[export]
unsafe extern "C" fn rust_move_page_tables(state: *mut State) -> usize {
    // SAFETY: The caller upholds the page-table move requirements.
    unsafe { move_page_tables(state) }
}
