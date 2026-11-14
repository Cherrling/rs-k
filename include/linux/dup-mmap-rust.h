/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_DUP_MMAP_RUST_H
#define _LINUX_DUP_MMAP_RUST_H

#include <linux/list.h>
#include <linux/mm.h>

#ifdef CONFIG_RUST_DUP_MMAP

struct rust_dup_mmap_state {
	struct vma_iterator vmi;
	struct list_head userfaultfd;
	unsigned long charge;
};

int rust_dup_mmap(struct mm_struct *mm, struct mm_struct *oldmm);

void rust_helper_dup_mmap_state_init(struct rust_dup_mmap_state *state,
				     struct mm_struct *mm);
int rust_helper_dup_mmap_lock_source(struct mm_struct *oldmm);
void rust_helper_dup_mmap_flush_cache(struct mm_struct *oldmm);
void rust_helper_dup_mmap_uprobe(struct mm_struct *oldmm,
				 struct mm_struct *mm);
void rust_helper_dup_mmap_lock_destination(struct mm_struct *mm);
void rust_helper_dup_mmap_exe_file(struct mm_struct *mm,
				   struct mm_struct *oldmm);
void rust_helper_dup_mmap_copy_accounting(struct mm_struct *mm,
					  struct mm_struct *oldmm);
int rust_helper_dup_mmap_copy_tree(struct mm_struct *mm,
				   struct mm_struct *oldmm);
void rust_helper_dup_mmap_clear_tree_rcu(struct rust_dup_mmap_state *state);
struct vm_area_struct *
rust_helper_dup_mmap_next(struct rust_dup_mmap_state *state);
int rust_helper_dup_mmap_source_start_write(struct vm_area_struct *vma);
bool rust_helper_dup_mmap_dontcopy(struct vm_area_struct *vma);
int rust_helper_dup_mmap_clear_dontcopy(struct rust_dup_mmap_state *state,
					struct vm_area_struct *vma);
void rust_helper_dup_mmap_unaccount_source(struct mm_struct *mm,
					   struct vm_area_struct *vma);
bool rust_helper_dup_mmap_is_account(struct vm_area_struct *vma);
unsigned long rust_helper_dup_mmap_nr_pages(struct vm_area_struct *vma);
bool rust_helper_dup_mmap_account_memory(struct mm_struct *mm,
					 unsigned long pages);
void rust_helper_dup_mmap_set_charge(struct rust_dup_mmap_state *state,
				     unsigned long charge);
void
rust_helper_dup_mmap_unaccount_charge(struct rust_dup_mmap_state *state);
struct vm_area_struct *
rust_helper_dup_mmap_clone(struct vm_area_struct *vma);
int rust_helper_dup_mmap_dup_policy(struct vm_area_struct *source,
				    struct vm_area_struct *duplicate);
void rust_helper_dup_mmap_release_policy(struct vm_area_struct *vma);
void rust_helper_dup_mmap_set_mm(struct vm_area_struct *vma,
				 struct mm_struct *mm);
int rust_helper_dup_mmap_dup_userfaultfd(struct vm_area_struct *vma,
					 struct rust_dup_mmap_state *state);
bool rust_helper_dup_mmap_wipeonfork(struct vm_area_struct *vma);
void rust_helper_dup_mmap_clear_anon(struct vm_area_struct *vma);
int rust_helper_dup_mmap_anon_fork(struct vm_area_struct *duplicate,
				   struct vm_area_struct *source);
void rust_helper_dup_mmap_free_vma(struct vm_area_struct *vma);
void rust_helper_dup_mmap_dest_start_write(struct vm_area_struct *vma);
void rust_helper_dup_mmap_clear_locked(struct vm_area_struct *vma);
bool rust_helper_dup_mmap_is_hugetlb(struct vm_area_struct *vma);
void rust_helper_dup_mmap_dup_hugetlb(struct vm_area_struct *vma);
void rust_helper_dup_mmap_store(struct rust_dup_mmap_state *state,
				struct vm_area_struct *vma);
void rust_helper_dup_mmap_inc_map_count(struct mm_struct *mm);
bool rust_helper_dup_mmap_has_open(struct vm_area_struct *vma);
void rust_helper_dup_mmap_call_open(struct vm_area_struct *vma);
struct file *rust_helper_dup_mmap_file(struct vm_area_struct *vma);
struct address_space *rust_helper_dup_mmap_mapping(struct file *file);
void rust_helper_dup_mmap_get_file(struct file *file);
void rust_helper_dup_mmap_rmap_lock(struct address_space *mapping);
bool rust_helper_dup_mmap_shared_maywrite(struct vm_area_struct *vma);
void rust_helper_dup_mmap_allow_writable(struct address_space *mapping);
void rust_helper_dup_mmap_rmap_tree_lock(struct address_space *mapping);
void rust_helper_dup_mmap_rmap_insert_after(struct vm_area_struct *vma,
					    struct vm_area_struct *source,
					    struct address_space *mapping);
void rust_helper_dup_mmap_rmap_tree_unlock(struct address_space *mapping);
void rust_helper_dup_mmap_rmap_unlock(struct address_space *mapping);
int rust_helper_dup_mmap_copy_pages(struct vm_area_struct *vma,
				    struct vm_area_struct *source);
int rust_helper_dup_mmap_arch(struct mm_struct *oldmm,
			      struct mm_struct *mm);
unsigned long rust_helper_dup_mmap_map_count(struct mm_struct *mm);
void rust_helper_dup_mmap_oom_skip(struct mm_struct *mm);
unsigned long
rust_helper_dup_mmap_vma_start(struct vm_area_struct *vma);
void
rust_helper_dup_mmap_cleanup_partial(struct rust_dup_mmap_state *state,
				     struct mm_struct *mm,
				     unsigned long end);
void rust_helper_dup_mmap_destroy_tree(struct mm_struct *mm);
void rust_helper_dup_mmap_set_unstable(struct mm_struct *mm);
void rust_helper_dup_mmap_free_iterator(struct rust_dup_mmap_state *state);
void rust_helper_dup_mmap_set_tree_rcu(struct rust_dup_mmap_state *state);
void rust_helper_dup_mmap_ksm(struct mm_struct *mm,
			      struct mm_struct *oldmm);
void rust_helper_dup_mmap_khugepaged(struct mm_struct *mm,
				     struct mm_struct *oldmm);
void rust_helper_dup_mmap_unlock(struct mm_struct *mm);
void rust_helper_dup_mmap_flush_tlb(struct mm_struct *oldmm);
void
rust_helper_dup_mmap_userfault_complete(struct rust_dup_mmap_state *state);
void
rust_helper_dup_mmap_userfault_fail(struct rust_dup_mmap_state *state);

#ifdef CONFIG_DUP_MMAP_KUNIT_TEST

int rust_helper_dup_mmap_test_add_anon(struct mm_struct *mm,
				       unsigned long start,
				       unsigned long end,
				       vm_flags_t extra_flags);
void rust_dup_mmap_test_fail(struct mm_struct *mm, u32 stage);
u32 rust_dup_mmap_test_failure_count(void);

#define RUST_DUP_MMAP_TEST_FAIL_TREE		1
#define RUST_DUP_MMAP_TEST_FAIL_SOURCE_WRITE	2
#define RUST_DUP_MMAP_TEST_FAIL_DONTCOPY	3
#define RUST_DUP_MMAP_TEST_FAIL_ACCOUNT		4
#define RUST_DUP_MMAP_TEST_FAIL_VMA		5
#define RUST_DUP_MMAP_TEST_FAIL_POLICY		6
#define RUST_DUP_MMAP_TEST_FAIL_USERFAULTFD	7
#define RUST_DUP_MMAP_TEST_FAIL_ANON_VMA	8
#define RUST_DUP_MMAP_TEST_FAIL_PAGE_RANGE	9
#define RUST_DUP_MMAP_TEST_FAIL_ARCH		10

#endif /* CONFIG_DUP_MMAP_KUNIT_TEST */

#endif /* CONFIG_RUST_DUP_MMAP */

#endif /* _LINUX_DUP_MMAP_RUST_H */
