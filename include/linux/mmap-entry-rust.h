/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MMAP_ENTRY_RUST_H
#define _LINUX_MMAP_ENTRY_RUST_H

#include <linux/mman.h>
#include <linux/types.h>

struct rust_mmap_entry_state;

#ifdef CONFIG_RUST_MMAP_ENTRY

enum rust_mmap_entry_constant {
	RUST_MMAP_ENTRY_PROT_READ = PROT_READ,
	RUST_MMAP_ENTRY_PROT_WRITE = PROT_WRITE,
	RUST_MMAP_ENTRY_PROT_EXEC = PROT_EXEC,
	RUST_MMAP_ENTRY_MAP_TYPE = MAP_TYPE,
	RUST_MMAP_ENTRY_MAP_FIXED = MAP_FIXED,
	RUST_MMAP_ENTRY_MAP_FIXED_NOREPLACE = MAP_FIXED_NOREPLACE,
	RUST_MMAP_ENTRY_MAP_SHARED = MAP_SHARED,
	RUST_MMAP_ENTRY_MAP_SHARED_VALIDATE = MAP_SHARED_VALIDATE,
	RUST_MMAP_ENTRY_MAP_PRIVATE = MAP_PRIVATE,
	RUST_MMAP_ENTRY_MAP_DROPPABLE = MAP_DROPPABLE,
	RUST_MMAP_ENTRY_MAP_LOCKED = MAP_LOCKED,
	RUST_MMAP_ENTRY_MAP_HUGETLB = MAP_HUGETLB,
	RUST_MMAP_ENTRY_MAP_POPULATE = MAP_POPULATE,
	RUST_MMAP_ENTRY_MAP_NONBLOCK = MAP_NONBLOCK,
	RUST_MMAP_ENTRY_MAP_NORESERVE = MAP_NORESERVE,
};

enum rust_mmap_entry_file_fact {
	RUST_MMAP_ENTRY_FILE_READ = 1U << 0,
	RUST_MMAP_ENTRY_FILE_WRITE = 1U << 1,
	RUST_MMAP_ENTRY_FILE_SWAPFILE = 1U << 2,
	RUST_MMAP_ENTRY_FILE_APPEND = 1U << 3,
	RUST_MMAP_ENTRY_FILE_NOEXEC = 1U << 4,
	RUST_MMAP_ENTRY_FILE_HUGEPAGES = 1U << 5,
	RUST_MMAP_ENTRY_FILE_MMAP_SYNC = 1U << 6,
};

unsigned long rust_mmap_entry(struct rust_mmap_entry_state *state);

void rust_helper_mmap_entry_clear_populate(struct rust_mmap_entry_state *state);
void rust_helper_mmap_entry_assert_write_locked(struct rust_mmap_entry_state *state);
unsigned long rust_helper_mmap_entry_len(struct rust_mmap_entry_state *state);
unsigned long rust_helper_mmap_entry_prot(struct rust_mmap_entry_state *state);
unsigned long rust_helper_mmap_entry_flags(struct rust_mmap_entry_state *state);
bool rust_helper_mmap_entry_read_implies_exec(void);
bool rust_helper_mmap_entry_has_file(struct rust_mmap_entry_state *state);
unsigned int rust_helper_mmap_entry_file_facts(struct rust_mmap_entry_state *state);
void rust_helper_mmap_entry_add_prot_exec(struct rust_mmap_entry_state *state);
void rust_helper_mmap_entry_add_map_fixed(struct rust_mmap_entry_state *state);
void rust_helper_mmap_entry_round_hint(struct rust_mmap_entry_state *state);
void rust_helper_mmap_entry_align_len(struct rust_mmap_entry_state *state);
bool rust_helper_mmap_entry_pgoff_overflows(struct rust_mmap_entry_state *state);
bool rust_helper_mmap_entry_map_count_exceeded(struct rust_mmap_entry_state *state);
long rust_helper_mmap_entry_execute_only_pkey(struct rust_mmap_entry_state *state);
void rust_helper_mmap_entry_prepare_vma_flags(struct rust_mmap_entry_state *state,
					      int pkey);
void rust_helper_mmap_entry_get_unmapped_area(struct rust_mmap_entry_state *state);
unsigned long rust_helper_mmap_entry_addr(struct rust_mmap_entry_state *state);
bool rust_helper_mmap_entry_addr_is_error(struct rust_mmap_entry_state *state);
bool
rust_helper_mmap_entry_fixed_noreplace_intersects(struct rust_mmap_entry_state *state);
bool rust_helper_mmap_entry_can_do_mlock(void);
bool rust_helper_mmap_entry_mlock_future_ok(struct rust_mmap_entry_state *state);
bool rust_helper_mmap_entry_file_mmap_ok(struct rust_mmap_entry_state *state);
void rust_helper_mmap_entry_init_file_flags_mask(struct rust_mmap_entry_state *state);
void
rust_helper_mmap_entry_add_mmap_sync_flags_mask(struct rust_mmap_entry_state *state);
void rust_helper_mmap_entry_strip_legacy_flags(struct rust_mmap_entry_state *state);
bool rust_helper_mmap_entry_file_flags_unsupported(struct rust_mmap_entry_state *state);
void rust_helper_mmap_entry_set_shared_vma_flags(struct rust_mmap_entry_state *state);
void
rust_helper_mmap_entry_clear_shared_write_vma_flags(struct rust_mmap_entry_state *state);
bool rust_helper_mmap_entry_vma_exec(struct rust_mmap_entry_state *state);
void rust_helper_mmap_entry_clear_may_exec(struct rust_mmap_entry_state *state);
bool rust_helper_mmap_entry_can_mmap_file(struct rust_mmap_entry_state *state);
bool rust_helper_mmap_entry_vma_can_grow(struct rust_mmap_entry_state *state);
int rust_helper_mmap_entry_check_memfd_seals(struct rust_mmap_entry_state *state);
bool rust_helper_mmap_entry_vma_droppable_supported(void);
void rust_helper_mmap_entry_set_droppable_flag(struct rust_mmap_entry_state *state);
void
rust_helper_mmap_entry_set_droppable_lifetime_flags(struct rust_mmap_entry_state *state);
void rust_helper_mmap_entry_set_anon_pgoff_zero(struct rust_mmap_entry_state *state);
void rust_helper_mmap_entry_set_anon_pgoff(struct rust_mmap_entry_state *state);
bool rust_helper_mmap_entry_overcommit_not_never(void);
void rust_helper_mmap_entry_set_noreserve(struct rust_mmap_entry_state *state);
void rust_helper_mmap_entry_mmap_region(struct rust_mmap_entry_state *state);
bool rust_helper_mmap_entry_vma_locked(struct rust_mmap_entry_state *state);
void rust_helper_mmap_entry_set_populate(struct rust_mmap_entry_state *state);

#endif /* CONFIG_RUST_MMAP_ENTRY */

#endif /* _LINUX_MMAP_ENTRY_RUST_H */
