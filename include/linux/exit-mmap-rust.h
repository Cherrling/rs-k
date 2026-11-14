/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_EXIT_MMAP_RUST_H
#define _LINUX_EXIT_MMAP_RUST_H

#include <linux/mm.h>

#ifdef CONFIG_RUST_EXIT_MMAP

struct rust_exit_mmap_state;

void rust_exit_mmap(struct mm_struct *mm,
		    struct rust_exit_mmap_state *state);

void rust_helper_exit_mmap_notifier_release(struct mm_struct *mm);
void rust_helper_exit_mmap_state_init(struct rust_exit_mmap_state *state,
				      struct mm_struct *mm);
void rust_helper_exit_mmap_read_lock(struct mm_struct *mm);
void rust_helper_exit_mmap_read_unlock(struct mm_struct *mm);
void rust_helper_exit_mmap_write_lock(struct mm_struct *mm);
void rust_helper_exit_mmap_write_unlock(struct mm_struct *mm);
void rust_helper_exit_mmap_arch_exit(struct mm_struct *mm);
struct vm_area_struct *
rust_helper_exit_mmap_next(struct rust_exit_mmap_state *state);
void rust_helper_exit_mmap_unmap_init(struct rust_exit_mmap_state *state,
				      struct vm_area_struct *vma);
void rust_helper_exit_mmap_flush_cache(struct mm_struct *mm);
void rust_helper_exit_mmap_tlb_init(struct rust_exit_mmap_state *state,
				    struct mm_struct *mm);
void rust_helper_exit_mmap_unmap(struct rust_exit_mmap_state *state);
void rust_helper_exit_mmap_oom_skip(struct mm_struct *mm);
void
rust_helper_exit_mmap_set_write_locked(struct rust_exit_mmap_state *state);
void rust_helper_exit_mmap_clear_tree_rcu(struct mm_struct *mm);
void
rust_helper_exit_mmap_pgtable_init(struct rust_exit_mmap_state *state);
void
rust_helper_exit_mmap_free_pgtables(struct rust_exit_mmap_state *state);
void rust_helper_exit_mmap_tlb_finish(struct rust_exit_mmap_state *state);
void
rust_helper_exit_mmap_teardown_init(struct rust_exit_mmap_state *state,
				    struct vm_area_struct *vma);
unsigned long
rust_helper_exit_mmap_accounted_pages(struct vm_area_struct *vma);
void rust_helper_exit_mmap_mark_detached(struct vm_area_struct *vma);
void rust_helper_exit_mmap_remove_vma(struct vm_area_struct *vma);
void rust_helper_exit_mmap_cond_resched(void);
void rust_helper_exit_mmap_warn_count(struct mm_struct *mm, int count);
void rust_helper_exit_mmap_destroy_tree(struct mm_struct *mm);
void rust_helper_exit_mmap_trace(struct mm_struct *mm);
void rust_helper_exit_mmap_unaccount(unsigned long nr_accounted);

#ifdef CONFIG_EXIT_MMAP_KUNIT_TEST

int rust_helper_exit_mmap_test_add_anon(struct mm_struct *mm,
					unsigned long start,
					unsigned long end,
					vm_flags_t extra_flags);
void rust_exit_mmap_test_watch(struct mm_struct *mm);
u32 rust_exit_mmap_test_vma_runs(void);
u32 rust_exit_mmap_test_empty_runs(void);
u32 rust_exit_mmap_test_vma_count(void);
u64 rust_exit_mmap_test_accounted_pages(void);

#endif /* CONFIG_EXIT_MMAP_KUNIT_TEST */

#endif /* CONFIG_RUST_EXIT_MMAP */

#endif /* _LINUX_EXIT_MMAP_RUST_H */
