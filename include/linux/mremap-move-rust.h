/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MREMAP_MOVE_RUST_H
#define _LINUX_MREMAP_MOVE_RUST_H

#include <linux/types.h>

struct rust_mremap_move_state;

#ifdef CONFIG_RUST_MREMAP_MOVE

unsigned long rust_mremap_move(struct rust_mremap_move_state *state);

unsigned long
rust_helper_mremap_move_prepare(struct rust_mremap_move_state *state);
bool rust_helper_mremap_move_charge(struct rust_mremap_move_state *state);
void rust_helper_mremap_move_start_write(struct rust_mremap_move_state *state);
int rust_helper_mremap_move_copy(struct rust_mremap_move_state *state);
bool rust_helper_mremap_move_has_new_vma(struct rust_mremap_move_state *state);
bool rust_helper_mremap_move_is_dontunmap(struct rust_mremap_move_state *state);
unsigned long rust_helper_mremap_move_get_hiwater(void);
void rust_helper_mremap_move_set_hiwater(unsigned long hiwater_vm);
void rust_helper_mremap_move_dontunmap_complete(struct rust_mremap_move_state *state);
void rust_helper_mremap_move_account(struct rust_mremap_move_state *state);
void rust_helper_mremap_move_unmap_source(struct rust_mremap_move_state *state);
unsigned long
rust_helper_mremap_move_new_addr(struct rust_mremap_move_state *state);

#endif /* CONFIG_RUST_MREMAP_MOVE */

#endif /* _LINUX_MREMAP_MOVE_RUST_H */
