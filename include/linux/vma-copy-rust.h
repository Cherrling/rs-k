/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_VMA_COPY_RUST_H
#define _LINUX_VMA_COPY_RUST_H

#include <linux/types.h>

struct rust_vma_copy_state;
struct vm_area_struct;

#ifdef CONFIG_RUST_VMA_COPY

struct vm_area_struct *rust_vma_copy(struct rust_vma_copy_state *state);

bool rust_helper_vma_copy_has_anon_vma(struct rust_vma_copy_state *state);
bool rust_helper_vma_copy_is_anonymous(struct rust_vma_copy_state *state);
unsigned long
rust_helper_vma_copy_page_index(struct rust_vma_copy_state *state);
void rust_helper_vma_copy_set_anon_pgoff(struct rust_vma_copy_state *state,
					 unsigned long anon_pgoff);
void rust_helper_vma_copy_set_pgoff(struct rust_vma_copy_state *state,
				    unsigned long pgoff);
void rust_helper_vma_copy_set_can_self_merge(struct rust_vma_copy_state *state,
					     bool can_self_merge);
bool rust_helper_vma_copy_source_has_file(struct rust_vma_copy_state *state);
bool rust_helper_vma_copy_has_file(struct vm_area_struct *vma);
void rust_helper_vma_copy_skip_uprobe(struct rust_vma_copy_state *state);
struct vm_area_struct *
rust_helper_vma_copy_find_prev(struct rust_vma_copy_state *state);
unsigned long rust_helper_vma_copy_range_end(struct rust_vma_copy_state *state);
unsigned long rust_helper_vma_copy_vma_start(struct vm_area_struct *vma);
unsigned long rust_helper_vma_copy_vma_end(struct vm_area_struct *vma);
void rust_helper_vma_copy_prepare_merge(struct rust_vma_copy_state *state);
bool rust_helper_vma_copy_keep_source(struct rust_vma_copy_state *state);
struct vm_area_struct *
rust_helper_vma_copy_source(struct rust_vma_copy_state *state);
struct vm_area_struct *
rust_helper_vma_copy_prev(struct rust_vma_copy_state *state);
struct vm_area_struct *
rust_helper_vma_copy_next(struct rust_vma_copy_state *state);
void rust_helper_vma_copy_clear_prev(struct rust_vma_copy_state *state);
void rust_helper_vma_copy_clear_next(struct rust_vma_copy_state *state);
void rust_helper_vma_copy_clear_self_merge(struct rust_vma_copy_state *state);
struct vm_area_struct *
rust_helper_vma_copy_merge(struct rust_vma_copy_state *state);
unsigned long
rust_helper_vma_copy_old_start(struct rust_vma_copy_state *state);
void rust_helper_vma_copy_warn_replaced_self(struct rust_vma_copy_state *state,
					     struct vm_area_struct *vma);
void rust_helper_vma_copy_replace_source(struct rust_vma_copy_state *state,
					 struct vm_area_struct *vma);
unsigned long
rust_helper_vma_copy_start_pgoff(struct vm_area_struct *vma);
void rust_helper_vma_copy_set_need_rmap_locks(struct rust_vma_copy_state *state,
					      bool need_rmap_locks);
struct vm_area_struct *
rust_helper_vma_copy_dup(struct rust_vma_copy_state *state);
void rust_helper_vma_copy_set_range(struct rust_vma_copy_state *state,
				    struct vm_area_struct *vma);
int rust_helper_vma_copy_dup_policy(struct rust_vma_copy_state *state,
				    struct vm_area_struct *vma);
int rust_helper_vma_copy_anon_clone(struct rust_vma_copy_state *state,
				    struct vm_area_struct *vma);
bool rust_helper_vma_copy_has_open(struct vm_area_struct *vma);
void rust_helper_vma_copy_get_file(struct vm_area_struct *vma);
void rust_helper_vma_copy_open(struct vm_area_struct *vma);
int rust_helper_vma_copy_link(struct rust_vma_copy_state *state,
			      struct vm_area_struct *vma);
void rust_helper_vma_copy_fixup_hugetlb(struct vm_area_struct *vma);
void rust_helper_vma_copy_close(struct vm_area_struct *vma);
void rust_helper_vma_copy_fput(struct vm_area_struct *vma);
void rust_helper_vma_copy_unlink_anon(struct vm_area_struct *vma);
void rust_helper_vma_copy_policy_put(struct vm_area_struct *vma);
void rust_helper_vma_copy_free(struct vm_area_struct *vma);

#endif /* CONFIG_RUST_VMA_COPY */

#endif /* _LINUX_VMA_COPY_RUST_H */
