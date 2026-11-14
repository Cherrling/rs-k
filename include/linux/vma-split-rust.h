/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_VMA_SPLIT_RUST_H
#define _LINUX_VMA_SPLIT_RUST_H

#include <linux/types.h>
#include <linux/vma-merge-commit-rust.h>

struct mm_struct;
struct vm_area_struct;
struct vma_iterator;

#ifdef CONFIG_RUST_VMA_SPLIT

int rust_vma_split(struct vma_iterator *vmi, struct vm_area_struct *vma,
		   unsigned long addr, int new_below, bool check_map_count);

int rust_helper_vma_split_map_count(struct vm_area_struct *vma);
int rust_helper_vma_split_max_map_count(void);
void rust_helper_vma_split_warn_on_invalid(struct vm_area_struct *vma,
					   unsigned long addr);
bool rust_helper_vma_split_has_may_split(struct vm_area_struct *vma);
int rust_helper_vma_split_may_split(struct vm_area_struct *vma,
				    unsigned long addr);
struct vm_area_struct *
rust_helper_vma_split_dup(struct vm_area_struct *vma);
void rust_helper_vma_split_free(struct vm_area_struct *vma);
unsigned long rust_helper_vma_split_vma_start(struct vm_area_struct *vma);
unsigned long rust_helper_vma_split_vma_end(struct vm_area_struct *vma);
void rust_helper_vma_split_set_start(struct vm_area_struct *vma,
				     unsigned long start);
void rust_helper_vma_split_set_end(struct vm_area_struct *vma,
				   unsigned long end);
void rust_helper_vma_split_add_pgoff(struct vm_area_struct *target,
				     struct vm_area_struct *source,
				     unsigned long addr);
void rust_helper_vma_split_iter_config(struct vma_iterator *vmi,
				       unsigned long start, unsigned long end);
int rust_helper_vma_split_iter_prealloc(struct vma_iterator *vmi,
					struct vm_area_struct *vma);
void rust_helper_vma_split_iter_free(struct vma_iterator *vmi);
int rust_helper_vma_split_dup_policy(struct vm_area_struct *src,
				     struct vm_area_struct *dst);
int rust_helper_vma_split_anon_clone(struct vm_area_struct *dst,
				     struct vm_area_struct *src);
void rust_helper_vma_split_policy_put(struct vm_area_struct *vma);
bool rust_helper_vma_split_has_file(struct vm_area_struct *vma);
void rust_helper_vma_split_get_file(struct vm_area_struct *vma);
bool rust_helper_vma_split_has_open(struct vm_area_struct *vma);
void rust_helper_vma_split_open(struct vm_area_struct *vma);
void rust_helper_vma_split_start_write(struct vm_area_struct *vma);
void rust_helper_vma_split_init_prepare(struct vma_prepare *vp,
					struct vm_area_struct *vma,
					struct vm_area_struct *insert);
void rust_helper_vma_split_prepare(struct vma_prepare *vp);
void rust_helper_vma_split_adjust_trans_huge(struct vm_area_struct *vma,
					     unsigned long addr);
bool rust_helper_vma_split_is_hugetlb(struct vm_area_struct *vma);
void rust_helper_vma_split_hugetlb(struct vm_area_struct *vma,
				   unsigned long addr);
void rust_helper_vma_split_complete(struct vma_prepare *vp,
				    struct vma_iterator *vmi,
				    struct vm_area_struct *vma);
void rust_helper_vma_split_validate_mm(struct vm_area_struct *vma);
void rust_helper_vma_split_next(struct vma_iterator *vmi);
void rust_helper_vma_split_prev(struct vma_iterator *vmi);

#endif /* CONFIG_RUST_VMA_SPLIT */

#endif /* _LINUX_VMA_SPLIT_RUST_H */
