/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MREMAP_RESIZE_RUST_H
#define _LINUX_MREMAP_RESIZE_RUST_H

#include <linux/types.h>

struct rust_mremap_resize_state;

#define RUST_MREMAP_RESIZE_INVALID	0
#define RUST_MREMAP_RESIZE_NO_RESIZE	1
#define RUST_MREMAP_RESIZE_SHRINK	2
#define RUST_MREMAP_RESIZE_EXPAND	3

#ifdef CONFIG_RUST_MREMAP_RESIZE

unsigned long rust_mremap_resize(struct rust_mremap_resize_state *state);

unsigned int rust_helper_mremap_resize_type(struct rust_mremap_resize_state *state);
unsigned long
rust_helper_mremap_resize_addr(struct rust_mremap_resize_state *state);
unsigned long
rust_helper_mremap_resize_shrink(struct rust_mremap_resize_state *state);
bool rust_helper_mremap_resize_ends_at_vma_end(struct rust_mremap_resize_state *state);
bool rust_helper_mremap_resize_is_expandable(struct rust_mremap_resize_state *state);
bool rust_helper_mremap_resize_charge(struct rust_mremap_resize_state *state);
bool rust_helper_mremap_resize_merge(struct rust_mremap_resize_state *state);
void rust_helper_mremap_resize_uncharge(struct rust_mremap_resize_state *state);
void rust_helper_mremap_resize_account(struct rust_mremap_resize_state *state);
bool rust_helper_mremap_resize_is_movable(struct rust_mremap_resize_state *state);
unsigned long
rust_helper_mremap_resize_set_new_addr(struct rust_mremap_resize_state *state);
unsigned long
rust_helper_mremap_resize_move(struct rust_mremap_resize_state *state);
void rust_helper_mremap_resize_warn_invalid(void);

#endif /* CONFIG_RUST_MREMAP_RESIZE */

#endif /* _LINUX_MREMAP_RESIZE_RUST_H */
