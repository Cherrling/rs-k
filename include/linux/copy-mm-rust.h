/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_COPY_MM_RUST_H
#define _LINUX_COPY_MM_RUST_H

#include <linux/types.h>

struct mm_struct;
struct task_struct;

#ifdef CONFIG_RUST_COPY_MM

struct mm_struct *rust_mm_init(struct mm_struct *mm, struct task_struct *p);
int rust_copy_mm(u64 clone_flags, struct task_struct *tsk);

void rust_helper_copy_mm_mt_init(struct mm_struct *mm);
void rust_helper_copy_mm_mt_set_external_lock(struct mm_struct *mm);
void rust_helper_copy_mm_users_init(struct mm_struct *mm);
void rust_helper_copy_mm_count_init(struct mm_struct *mm);
void rust_helper_copy_mm_write_protect_seq_init(struct mm_struct *mm);
void rust_helper_copy_mm_mmap_lock_init(struct mm_struct *mm);
void rust_helper_copy_mm_mmlist_init(struct mm_struct *mm);
void rust_helper_copy_mm_pgtables_bytes_init(struct mm_struct *mm);
void rust_helper_copy_mm_map_count_init(struct mm_struct *mm);
void rust_helper_copy_mm_locked_vm_init(struct mm_struct *mm);
void rust_helper_copy_mm_pinned_vm_init(struct mm_struct *mm);
void rust_helper_copy_mm_rss_stat_init(struct mm_struct *mm);
void rust_helper_copy_mm_page_table_lock_init(struct mm_struct *mm);
void rust_helper_copy_mm_arg_lock_init(struct mm_struct *mm);
void rust_helper_copy_mm_cpumask_init(struct mm_struct *mm);
void rust_helper_copy_mm_aio_init(struct mm_struct *mm);
void rust_helper_copy_mm_owner_init(struct mm_struct *mm,
				    struct task_struct *task);
void rust_helper_copy_mm_pasid_init(struct mm_struct *mm);
void rust_helper_copy_mm_exe_file_init(struct mm_struct *mm);
void rust_helper_copy_mm_mmu_notifier_init(struct mm_struct *mm);
void rust_helper_copy_mm_tlb_flush_pending_init(struct mm_struct *mm);
void rust_helper_copy_mm_pmd_huge_pte_init(struct mm_struct *mm);
void rust_helper_copy_mm_uprobes_state_init(struct mm_struct *mm);
void rust_helper_copy_mm_hugetlb_count_init(struct mm_struct *mm);
void rust_helper_copy_mm_futex_init(struct mm_struct *mm);
void rust_helper_copy_mm_flags_clear_all(struct mm_struct *mm);
struct mm_struct *rust_helper_copy_mm_current_mm(void);
unsigned long rust_helper_copy_mm_flags_word(struct mm_struct *mm);
unsigned long rust_helper_copy_mm_init_legacy_flags(unsigned long flags);
void rust_helper_copy_mm_set_flags(struct mm_struct *mm, unsigned long flags);
unsigned long rust_helper_copy_mm_coredump_filter(void);
unsigned long rust_helper_copy_mm_def_flags(struct mm_struct *mm);
void rust_helper_copy_mm_set_def_flags(struct mm_struct *mm,
				       unsigned long def_flags);
int rust_helper_copy_mm_alloc_pgd(struct mm_struct *mm);
int rust_helper_copy_mm_alloc_id(struct mm_struct *mm);
int rust_helper_copy_mm_init_context(struct task_struct *task,
				     struct mm_struct *mm);
int rust_helper_copy_mm_alloc_cid(struct mm_struct *mm,
				  struct task_struct *task);
int rust_helper_copy_mm_alloc_sched(struct mm_struct *mm);
int rust_helper_copy_mm_init_rss(struct mm_struct *mm);
void rust_helper_copy_mm_destroy_sched(struct mm_struct *mm);
void rust_helper_copy_mm_destroy_cid(struct mm_struct *mm);
void rust_helper_copy_mm_destroy_context(struct mm_struct *mm);
void rust_helper_copy_mm_free_id(struct mm_struct *mm);
void rust_helper_copy_mm_free_pgd(struct mm_struct *mm);
void rust_helper_copy_mm_free(struct mm_struct *mm);
void rust_helper_copy_mm_lru_gen_init(struct mm_struct *mm);
struct mm_struct *rust_helper_copy_mm_allocate(void);
void rust_helper_copy_mm_uprobe_start(void);
void rust_helper_copy_mm_uprobe_end(void);
int rust_helper_copy_mm_dup_mmap(struct mm_struct *mm,
				 struct mm_struct *oldmm);
void rust_helper_copy_mm_set_hiwater(struct mm_struct *mm);
int rust_helper_copy_mm_get_binfmt(struct mm_struct *mm);
void rust_helper_copy_mm_clear_binfmt(struct mm_struct *mm);
void rust_helper_copy_mm_clear_owner(struct mm_struct *mm);
void rust_helper_copy_mm_sched_cache_fork(struct task_struct *task);

#ifdef CONFIG_COPY_MM_KUNIT_TEST
#define RUST_COPY_MM_TEST_FAIL_PGD		1
#define RUST_COPY_MM_TEST_FAIL_ID		2
#define RUST_COPY_MM_TEST_FAIL_CONTEXT		3
#define RUST_COPY_MM_TEST_FAIL_CID		4
#define RUST_COPY_MM_TEST_FAIL_SCHED		5
#define RUST_COPY_MM_TEST_FAIL_RSS		6
#define RUST_COPY_MM_TEST_FAIL_DUP_MMAP		1
#define RUST_COPY_MM_TEST_FAIL_DUP_BINFMT	2

struct mm_struct *rust_copy_mm_test_dup(struct task_struct *task,
					struct mm_struct *oldmm);
int rust_copy_mm_test_kernel_thread(struct task_struct *task);
void rust_copy_mm_test_fail_mm_init(u32 stage);
u32 rust_copy_mm_test_init_failure_count(void);
void rust_copy_mm_test_fail_dup(u32 stage);
u32 rust_copy_mm_test_dup_failure_count(void);
#endif

#endif /* CONFIG_RUST_COPY_MM */

#endif /* _LINUX_COPY_MM_RUST_H */
