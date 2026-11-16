/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Internal interface between the Maple Tree core and the native Rust loader.
 */

#ifndef _LINUX_MAPLE_TREE_RUST_H
#define _LINUX_MAPLE_TREE_RUST_H

#include <linux/maple_tree.h>

#if IS_ENABLED(CONFIG_RUST_MAPLE_TREE_LOAD) || \
	IS_ENABLED(CONFIG_RUST_MAPLE_TREE_WALK)

void *rust_mtree_load(struct maple_tree *mt, unsigned long index);
void *rust_mas_walk(struct ma_state *mas);

void rust_maple_tree_init_lock_check(struct ma_state *mas);
void rust_maple_tree_may_init_lock_check(struct ma_state *mas);
void *rust_maple_tree_root(struct ma_state *mas);
void *rust_maple_tree_safe_root(const void *enode);
bool rust_maple_tree_is_node(const void *entry);
bool rust_maple_tree_enode_is_dead(const void *enode);
void *rust_maple_tree_to_node(const void *enode);
enum maple_type rust_maple_tree_node_type(const void *enode);
unsigned long *rust_maple_tree_pivots(struct maple_node *node,
				      enum maple_type type);
void __rcu **rust_maple_tree_slots(struct maple_node *node,
				   enum maple_type type);
unsigned char rust_maple_tree_pivot_count(enum maple_type type);
unsigned char rust_maple_tree_data_end(struct maple_node *node,
				       enum maple_type type,
				       unsigned long *pivots,
				       unsigned long max);
void *rust_maple_tree_slot(struct ma_state *mas, void __rcu **slots,
			   unsigned char offset);
bool rust_maple_tree_node_is_dead(const struct maple_node *node);
bool rust_maple_tree_is_leaf(enum maple_type type);
unsigned char rust_maple_tree_node_slots(void);
void rust_maple_tree_load_trace(struct ma_state *mas);
bool rust_maple_tree_is_zero(const void *entry);

#endif /* CONFIG_RUST_MAPLE_TREE_LOAD || CONFIG_RUST_MAPLE_TREE_WALK */

#endif /* _LINUX_MAPLE_TREE_RUST_H */
