/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_VMA_MERGE_EXISTING_RUST_H
#define _LINUX_VMA_MERGE_EXISTING_RUST_H

#include <linux/mm.h>

/* Existing-range VMA merge state. */
struct rust_vme_state {
	struct vma_merge_struct *vmg;
	vma_flags_t sticky_flags;
	struct vm_area_struct *anon_dup;
};

#ifdef CONFIG_RUST_VMA_MERGE_EXISTING

struct vm_area_struct *
rust_vma_merge_existing(struct rust_vme_state *state);

void rust_helper_vma_merge_existing_validate(struct rust_vme_state *state);
void rust_helper_vma_merge_existing_set_nomerge(struct rust_vme_state *state);
void rust_helper_vma_merge_existing_set_success(struct rust_vme_state *state);
void rust_helper_vma_merge_existing_set_nomem(struct rust_vme_state *state);
bool rust_helper_vma_merge_existing_is_special(struct rust_vme_state *state);
bool rust_helper_vma_merge_existing_can_merge_left(struct rust_vme_state *state);
bool rust_helper_vma_merge_existing_can_merge_right(struct rust_vme_state *state,
						    bool can_merge_left);
bool rust_helper_vma_merge_existing_give_up_on_oom(struct rust_vme_state *state);
struct vm_area_struct *
rust_helper_vma_merge_existing_middle(struct rust_vme_state *state);
struct vm_area_struct *
rust_helper_vma_merge_existing_prev(struct rust_vme_state *state);
unsigned long
rust_helper_vma_merge_existing_start(struct rust_vme_state *state);
unsigned long
rust_helper_vma_merge_existing_end(struct rust_vme_state *state);
unsigned long rust_helper_vma_merge_existing_vma_start(struct vm_area_struct *vma);
unsigned long rust_helper_vma_merge_existing_vma_end(struct vm_area_struct *vma);
unsigned long rust_helper_vma_merge_existing_vma_start_pgoff(struct vm_area_struct *vma);
unsigned long rust_helper_vma_merge_existing_vma_start_anon_pgoff(struct vm_area_struct *vma);
unsigned long rust_helper_vma_merge_existing_pages(struct rust_vme_state *state);
struct vm_area_struct *
rust_helper_vma_merge_existing_iter_next_range(struct rust_vme_state *state);
void rust_helper_vma_merge_existing_iter_prev_range(struct rust_vme_state *state);
void rust_helper_vma_merge_existing_iter_set(struct rust_vme_state *state,
					     unsigned long start);
void rust_helper_vma_merge_existing_iter_load(struct rust_vme_state *state);
void rust_helper_vma_merge_existing_set_next(struct rust_vme_state *state,
					     struct vm_area_struct *next);
void rust_helper_vma_merge_existing_prev_range(struct rust_vme_state *state);
bool rust_helper_vma_merge_existing_can_remove(struct vm_area_struct *vma);
void rust_helper_vma_merge_existing_start_write(struct vm_area_struct *vma);
void rust_helper_vma_merge_existing_add_target_sticky(struct rust_vme_state *state,
						      struct vm_area_struct *vma);
void rust_helper_vma_merge_existing_apply_sticky(struct rust_vme_state *state,
						 struct vm_area_struct *vma);
void rust_helper_vma_merge_existing_set_remove_middle(struct rust_vme_state *state,
						      bool remove);
void rust_helper_vma_merge_existing_set_remove_next(struct rust_vme_state *state,
						    bool remove);
void
rust_helper_vma_merge_existing_set_adjust_middle_start(struct rust_vme_state *state,
						       bool adjust);
void
rust_helper_vma_merge_existing_set_adjust_next_start(struct rust_vme_state *state,
						     bool adjust);
void rust_helper_vma_merge_existing_set_target(struct rust_vme_state *state,
					       struct vm_area_struct *target);
void rust_helper_vma_merge_existing_set_start(struct rust_vme_state *state,
					      unsigned long start);
void rust_helper_vma_merge_existing_set_end(struct rust_vme_state *state,
					    unsigned long end);
void
rust_helper_vma_merge_existing_set_start_pgoffs(struct rust_vme_state *state,
						struct vm_area_struct *vma);
void rust_helper_vma_merge_existing_set_pgoffs(struct rust_vme_state *state,
					       unsigned long pgoff,
					       unsigned long anon_pgoff);
bool rust_helper_vma_merge_existing_vma_has_anon_vma(struct vm_area_struct *vma);
int rust_helper_vma_merge_existing_dup_anon(struct rust_vme_state *state,
					    struct vm_area_struct *dst,
					    struct vm_area_struct *src);
void
rust_helper_vma_merge_existing_unlink_anon(struct rust_vme_state *state);
int rust_helper_vma_merge_existing_commit(struct rust_vme_state *state);
void
rust_helper_vma_merge_existing_khugepaged_enter(struct rust_vme_state *state,
						struct vm_area_struct *target);
void rust_helper_vma_merge_existing_debug_right(struct rust_vme_state *state,
						bool merge_right);

#endif /* CONFIG_RUST_VMA_MERGE_EXISTING */

#endif /* _LINUX_VMA_MERGE_EXISTING_RUST_H */
