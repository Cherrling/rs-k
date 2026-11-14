/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_VMA_MERGE_NEW_RANGE_RUST_H
#define _LINUX_VMA_MERGE_NEW_RANGE_RUST_H

#include <linux/types.h>

struct vma_merge_struct;
struct vm_area_struct;

struct rust_vma_merge_new_range_state {
	struct vma_merge_struct *vmg;
	struct vm_area_struct *prev;
	struct vm_area_struct *next;
	unsigned long end;
	bool just_expand;
};

#ifdef CONFIG_RUST_VMA_MERGE_NEW_RANGE

struct vm_area_struct *
rust_vma_merge_new_range(struct rust_vma_merge_new_range_state *state);

void rust_helper_vma_merge_new_range_validate(struct vma_merge_struct *vmg,
					      unsigned long end);
void rust_helper_vma_merge_new_range_set_nomerge(struct vma_merge_struct *vmg);
void rust_helper_vma_merge_new_range_set_success(struct vma_merge_struct *vmg);
bool rust_helper_vma_merge_new_range_is_special(struct vma_merge_struct *vmg);
bool rust_helper_vma_merge_new_range_can_merge_left(struct vma_merge_struct *vmg);
bool rust_helper_vma_merge_new_range_can_merge_right(struct vma_merge_struct *vmg,
						     bool can_merge_left);
unsigned long
rust_helper_vma_merge_new_range_prev_start(struct vm_area_struct *prev);
unsigned long
rust_helper_vma_merge_new_range_next_end(struct vm_area_struct *next);
bool rust_helper_vma_merge_new_range_can_remove_next(struct vm_area_struct *next);
void rust_helper_vma_merge_new_range_set_end(struct vma_merge_struct *vmg,
					     unsigned long end);
void rust_helper_vma_merge_new_range_set_start(struct vma_merge_struct *vmg,
					       unsigned long start);
void rust_helper_vma_merge_new_range_set_target(struct vma_merge_struct *vmg,
						struct vm_area_struct *target);
struct vm_area_struct *
rust_helper_vma_merge_new_range_target(struct vma_merge_struct *vmg);
void rust_helper_vma_merge_new_range_set_start_pgoffs(struct vma_merge_struct *vmg,
						      struct vm_area_struct *vma);
void rust_helper_vma_merge_new_range_prev(struct vma_merge_struct *vmg);
int rust_helper_vma_merge_new_range_expand(struct vma_merge_struct *vmg);
void rust_helper_vma_merge_new_range_khugepaged_enter(struct vma_merge_struct *vmg,
						      struct vm_area_struct *target);

#endif /* CONFIG_RUST_VMA_MERGE_NEW_RANGE */

#endif /* _LINUX_VMA_MERGE_NEW_RANGE_RUST_H */
