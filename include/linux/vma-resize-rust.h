/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_VMA_RESIZE_RUST_H
#define _LINUX_VMA_RESIZE_RUST_H

#include <linux/mm.h>

struct vma_merge_struct;
struct vma_iterator;
struct vm_area_struct;
struct vma_prepare;

#ifdef CONFIG_RUST_VMA_EXPAND

int rust_vma_expand(struct vma_merge_struct *vmg);

void rust_helper_vma_expand_assert_write_locked(struct vma_merge_struct *vmg);
struct vm_area_struct *
rust_helper_vma_expand_target(struct vma_merge_struct *vmg);
struct vm_area_struct *
rust_helper_vma_expand_next(struct vma_merge_struct *vmg);
struct vm_area_struct *
rust_helper_vma_expand_copied_from(struct vma_merge_struct *vmg);
unsigned long rust_helper_vma_expand_end(struct vma_merge_struct *vmg);
unsigned long rust_helper_vma_expand_vma_end(struct vm_area_struct *vma);
void rust_helper_vma_expand_init_sticky(vma_flags_t *sticky,
					struct vma_merge_struct *vmg);
void rust_helper_vma_expand_add_sticky(vma_flags_t *sticky,
				       struct vm_area_struct *vma);
void rust_helper_vma_expand_apply_sticky(struct vm_area_struct *vma,
					 const vma_flags_t *sticky);
void rust_helper_vma_expand_validate(struct vma_merge_struct *vmg,
				     struct vm_area_struct *target,
				     struct vm_area_struct *next,
				     bool remove_next);
void rust_helper_vma_expand_start_write(struct vm_area_struct *vma);
void rust_helper_vma_expand_set_remove_next(struct vma_merge_struct *vmg,
					    bool remove);
int rust_helper_vma_expand_dup_anon(struct vm_area_struct *target,
				    struct vm_area_struct *src,
				    struct vm_area_struct **anon_dup);
int rust_helper_vma_expand_commit(struct vma_merge_struct *vmg);
void rust_helper_vma_expand_unlink_anon(struct vm_area_struct *anon_dup);
bool rust_helper_vma_expand_give_up_on_oom(struct vma_merge_struct *vmg);
void rust_helper_vma_expand_set_nomem(struct vma_merge_struct *vmg);

#endif /* CONFIG_RUST_VMA_EXPAND */

#ifdef CONFIG_RUST_VMA_SHRINK

int rust_vma_shrink(struct vma_iterator *vmi, struct vm_area_struct *vma,
		    unsigned long end);

void rust_helper_vma_shrink_validate(struct vm_area_struct *vma,
				     unsigned long end);
unsigned long rust_helper_vma_shrink_vma_end(struct vm_area_struct *vma);
void rust_helper_vma_shrink_iter_config(struct vma_iterator *vmi,
					unsigned long start, unsigned long end);
int rust_helper_vma_shrink_iter_prealloc(struct vma_iterator *vmi);
void rust_helper_vma_shrink_start_write(struct vm_area_struct *vma);
void rust_helper_vma_shrink_init_prepare(struct vma_prepare *vp,
					 struct vm_area_struct *vma);
void rust_helper_vma_shrink_prepare(struct vma_prepare *vp);
void rust_helper_vma_shrink_adjust_trans_huge(struct vm_area_struct *vma,
					      unsigned long end);
void rust_helper_vma_shrink_iter_clear(struct vma_iterator *vmi);
void rust_helper_vma_shrink_set_end(struct vm_area_struct *vma,
				    unsigned long end);
void rust_helper_vma_shrink_complete(struct vma_prepare *vp,
				     struct vma_iterator *vmi,
				     struct vm_area_struct *vma);
void rust_helper_vma_shrink_validate_mm(struct vm_area_struct *vma);

#endif /* CONFIG_RUST_VMA_SHRINK */

#endif /* _LINUX_VMA_RESIZE_RUST_H */
