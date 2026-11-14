/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_VMA_MUNMAP_COMPLETE_RUST_H
#define _LINUX_VMA_MUNMAP_COMPLETE_RUST_H

#include <linux/maple_tree.h>
#include <linux/mm.h>

struct rust_vma_munmap_complete_state {
	struct mm_struct *mm;
	struct vm_area_struct *first;
	struct ma_state *mas_detach;
	unsigned long start;
	unsigned long end;
	unsigned long unmap_start;
	unsigned long unmap_end;
	int vma_count;
	bool unlock;
	bool clear_ptes;
	unsigned long nr_pages;
	unsigned long locked_vm;
	unsigned long nr_accounted;
	unsigned long exec_vm;
	unsigned long stack_vm;
	unsigned long data_vm;
};

struct rust_vma_munmap_align_state;

#ifdef CONFIG_RUST_VMA_MUNMAP_COMPLETE

void rust_vma_munmap_complete(struct rust_vma_munmap_complete_state *state);
void rust_vma_munmap_reattach(struct ma_state *mas_detach);
int rust_do_vmi_align_munmap(struct rust_vma_munmap_align_state *state);

void rust_helper_vma_munmap_adjust_counts(struct mm_struct *mm,
					  int vma_count,
					  unsigned long locked_vm);
void rust_helper_vma_munmap_downgrade(struct mm_struct *mm);
void rust_helper_vma_munmap_clear_ptes(
		const struct rust_vma_munmap_complete_state *state);
void rust_helper_vma_munmap_update_hiwater(struct mm_struct *mm);
void rust_helper_vma_munmap_sub_total_vm(struct mm_struct *mm,
					 unsigned long pages);
void rust_helper_vma_munmap_adjust_stats(struct mm_struct *mm,
					 unsigned long exec_vm,
					 unsigned long stack_vm,
					 unsigned long data_vm);
void rust_helper_vma_munmap_mas_reset(struct ma_state *mas_detach);
struct vm_area_struct *
rust_helper_vma_munmap_mas_find(struct ma_state *mas_detach);
void rust_helper_vma_munmap_remove_vma(struct vm_area_struct *vma);
void rust_helper_vma_munmap_unaccount(unsigned long pages);
void rust_helper_vma_munmap_validate(struct mm_struct *mm);
void rust_helper_vma_munmap_read_unlock(struct mm_struct *mm);
void rust_helper_vma_munmap_mark_attached(struct vm_area_struct *vma);
void rust_helper_vma_munmap_destroy_tree(struct ma_state *mas_detach);

int rust_helper_vma_munmap_align_gather(
		struct rust_vma_munmap_align_state *state);
int rust_helper_vma_munmap_align_clear_tree(
		struct rust_vma_munmap_align_state *state);
void rust_helper_vma_munmap_align_snapshot(
		struct rust_vma_munmap_align_state *state,
		struct rust_vma_munmap_complete_state *snapshot);
void rust_helper_vma_munmap_align_validate(
		struct rust_vma_munmap_align_state *state);

#endif /* CONFIG_RUST_VMA_MUNMAP_COMPLETE */

#endif /* _LINUX_VMA_MUNMAP_COMPLETE_RUST_H */
