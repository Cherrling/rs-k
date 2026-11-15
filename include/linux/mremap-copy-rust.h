/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MREMAP_COPY_RUST_H
#define _LINUX_MREMAP_COPY_RUST_H

#include <linux/types.h>

struct rust_mremap_copy_state;
struct vm_area_struct;

#ifdef CONFIG_RUST_MREMAP_COPY

int rust_mremap_copy(struct rust_mremap_copy_state *state);

struct vm_area_struct *
rust_helper_mremap_copy_vma(struct rust_mremap_copy_state *state);
bool rust_helper_mremap_copy_source_changed(struct rust_mremap_copy_state *state);
void rust_helper_mremap_copy_mark_vmi_invalid(struct rust_mremap_copy_state *state);
void rust_helper_mremap_copy_commit_source(struct rust_mremap_copy_state *state);
void rust_helper_mremap_copy_set_new_vma(struct rust_mremap_copy_state *state,
					 struct vm_area_struct *vma);
unsigned long
rust_helper_mremap_copy_page_tables(struct rust_mremap_copy_state *state);
unsigned long
rust_helper_mremap_copy_old_len(struct rust_mremap_copy_state *state);
bool rust_helper_mremap_copy_has_mremap(struct rust_mremap_copy_state *state);
int rust_helper_mremap_copy_invoke_mremap(struct rust_mremap_copy_state *state);
void rust_helper_mremap_copy_prepare_revert(struct rust_mremap_copy_state *state,
					    unsigned long moved_len);
void rust_helper_mremap_copy_page_tables_back(struct rust_mremap_copy_state *state);
bool rust_helper_mremap_copy_keeps_source(struct rust_mremap_copy_state *state);
void rust_helper_mremap_copy_uncharge(struct rust_mremap_copy_state *state);
void rust_helper_mremap_copy_set_error_destination(struct rust_mremap_copy_state *state);
void rust_helper_mremap_copy_userfaultfd_prepare(struct rust_mremap_copy_state *state);
void rust_helper_mremap_copy_fixup_hugetlb(struct rust_mremap_copy_state *state);
void rust_helper_mremap_copy_set_output(struct rust_mremap_copy_state *state,
					struct vm_area_struct *vma);

#endif /* CONFIG_RUST_MREMAP_COPY */

#endif /* _LINUX_MREMAP_COPY_RUST_H */
