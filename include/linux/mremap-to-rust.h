/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MREMAP_TO_RUST_H
#define _LINUX_MREMAP_TO_RUST_H

#include <linux/types.h>

struct rust_mremap_to_state;

#ifdef CONFIG_RUST_MREMAP_TO

unsigned long rust_mremap_to(struct rust_mremap_to_state *state);

bool rust_helper_mremap_to_is_fixed(struct rust_mremap_to_state *state);
unsigned long
rust_helper_mremap_to_unmap_destination(struct rust_mremap_to_state *state);
void rust_helper_mremap_to_invalidate_source(struct rust_mremap_to_state *state);
bool rust_helper_mremap_to_reload_source(struct rust_mremap_to_state *state);
bool rust_helper_mremap_to_is_shrink(struct rust_mremap_to_state *state);
unsigned long
rust_helper_mremap_to_shrink_source(struct rust_mremap_to_state *state);
void rust_helper_mremap_to_shrink_complete(struct rust_mremap_to_state *state);
bool rust_helper_mremap_to_keeps_source(struct rust_mremap_to_state *state);
bool rust_helper_mremap_to_can_expand_for_dontunmap(struct rust_mremap_to_state *state);
unsigned long
rust_helper_mremap_to_set_new_addr(struct rust_mremap_to_state *state);
unsigned long rust_helper_mremap_to_move(struct rust_mremap_to_state *state);

#endif /* CONFIG_RUST_MREMAP_TO */

#endif /* _LINUX_MREMAP_TO_RUST_H */
