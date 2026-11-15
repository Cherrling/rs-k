/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MREMAP_BATCH_RUST_H
#define _LINUX_MREMAP_BATCH_RUST_H

#include <linux/types.h>

struct rust_mremap_batch_state;
struct vma_remap_struct;
struct vm_area_struct;

#ifdef CONFIG_RUST_MREMAP_BATCH

unsigned long rust_mremap_batch(struct rust_mremap_batch_state *state);

void
rust_helper_mremap_batch_state_init(struct rust_mremap_batch_state *state,
				    struct vma_remap_struct *vrm);
unsigned long
rust_helper_mremap_batch_start(struct rust_mremap_batch_state *state);
unsigned long
rust_helper_mremap_batch_end(struct rust_mremap_batch_state *state);
unsigned long
rust_helper_mremap_batch_new_addr(struct rust_mremap_batch_state *state);
unsigned long
rust_helper_mremap_batch_vma_start(struct vm_area_struct *vma);
unsigned long
rust_helper_mremap_batch_vma_end(struct vm_area_struct *vma);
unsigned long
rust_helper_mremap_batch_new_len(struct rust_mremap_batch_state *state);
struct vm_area_struct *
rust_helper_mremap_batch_next(struct rust_mremap_batch_state *state);
void
rust_helper_mremap_batch_configure(struct rust_mremap_batch_state *state,
				   struct vm_area_struct *vma,
				   unsigned long addr,
				   unsigned long new_addr,
				   unsigned long len);
bool rust_helper_mremap_batch_multi_allowed(struct vm_area_struct *vma);
unsigned long
rust_helper_mremap_batch_check_prepare(struct rust_mremap_batch_state *state);
unsigned long
rust_helper_mremap_batch_mremap_to(struct rust_mremap_batch_state *state);
bool rust_helper_mremap_batch_is_error_value(unsigned long value);
bool
rust_helper_mremap_batch_mmap_locked(struct rust_mremap_batch_state *state);
bool
rust_helper_mremap_batch_populate_expand(struct rust_mremap_batch_state *state);
bool
rust_helper_mremap_batch_needs_invalidate(struct rust_mremap_batch_state *state);
void rust_helper_mremap_batch_warn_multi_destination(bool condition);
void rust_helper_mremap_batch_warn_locked(bool condition);
void rust_helper_mremap_batch_warn_populate(bool condition);
void
rust_helper_mremap_batch_invalidate(struct rust_mremap_batch_state *state);

#endif /* CONFIG_RUST_MREMAP_BATCH */

#endif /* _LINUX_MREMAP_BATCH_RUST_H */
