/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Internal interface between the Maple Tree core and the native Rust loader.
 */

#ifndef _LINUX_MAPLE_TREE_RUST_H
#define _LINUX_MAPLE_TREE_RUST_H

#include <linux/maple_tree.h>

#ifdef CONFIG_RUST_MAPLE_TREE_LOAD

void *rust_mtree_load(struct maple_tree *mt, unsigned long index);

void rust_maple_tree_load_init_lock_check(struct ma_state *mas);
void *rust_maple_tree_load_root(struct ma_state *mas);
void *rust_maple_tree_load_safe_root(const void *enode);
bool rust_maple_tree_load_is_node(const void *entry);
bool rust_maple_tree_load_enode_is_dead(const void *enode);
void *rust_maple_tree_load_to_node(const void *enode);
enum maple_type rust_maple_tree_load_node_type(const void *enode);
unsigned long *rust_maple_tree_load_pivots(struct maple_node *node,
					   enum maple_type type);
void __rcu **rust_maple_tree_load_slots(struct maple_node *node,
					enum maple_type type);
unsigned char rust_maple_tree_load_pivot_count(enum maple_type type);
void *rust_maple_tree_load_slot(struct ma_state *mas,
				void __rcu **slots, unsigned char offset);
bool rust_maple_tree_load_node_is_dead(const struct maple_node *node);
bool rust_maple_tree_load_is_leaf(enum maple_type type);
unsigned char rust_maple_tree_load_node_slots(void);
void rust_maple_tree_load_trace(struct ma_state *mas);
bool rust_maple_tree_load_is_zero(const void *entry);

#endif /* CONFIG_RUST_MAPLE_TREE_LOAD */

#endif /* _LINUX_MAPLE_TREE_RUST_H */
