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

#ifdef CONFIG_RUST_MAPLE_TREE_NEXT

void *rust_mas_next(struct ma_state *mas, unsigned long max);
void *rust_mas_next_range(struct ma_state *mas, unsigned long max);
int rust_mas_ascend(struct ma_state *mas);
int rust_mas_next_node(struct ma_state *mas, struct maple_node *node,
		       unsigned long max);
void *rust_mas_next_slot(struct ma_state *mas, unsigned long max, bool empty);

bool rust_maple_tree_node_is_root(const struct maple_node *node);
bool rust_maple_tree_enode_is_root(const struct maple_enode *enode);
struct maple_node *rust_maple_tree_parent(const struct maple_enode *enode);
enum maple_type rust_maple_tree_parent_type(struct ma_state *mas,
					    struct maple_enode *enode);
unsigned int rust_maple_tree_parent_slot(const struct maple_enode *enode);
struct maple_enode *rust_maple_tree_make_node(const struct maple_node *node,
					      enum maple_type type);
void rust_maple_tree_lock_check(struct ma_state *mas);
bool rust_maple_tree_warn_on_once(bool condition);

#endif /* CONFIG_RUST_MAPLE_TREE_NEXT */

#ifdef CONFIG_RUST_MAPLE_TREE_PREV

void *rust_mas_prev(struct ma_state *mas, unsigned long min);
void *rust_mas_prev_range(struct ma_state *mas, unsigned long min);
void *rust_mas_find_rev(struct ma_state *mas, unsigned long min);
void *rust_mas_find_range_rev(struct ma_state *mas, unsigned long min);
int rust_mas_prev_node(struct ma_state *mas, unsigned long min);
void *rust_mas_prev_slot(struct ma_state *mas, unsigned long min, bool empty);

#endif /* CONFIG_RUST_MAPLE_TREE_PREV */

#ifdef CONFIG_RUST_MAPLE_TREE_FIND

void *rust_mas_find(struct ma_state *mas, unsigned long max);
void *rust_mas_find_range(struct ma_state *mas, unsigned long max);

#endif /* CONFIG_RUST_MAPLE_TREE_FIND */

#ifdef CONFIG_RUST_MAPLE_TREE_PREALLOC

int rust_mas_preallocate(struct ma_state *mas, void *entry, gfp_t gfp);

bool rust_maple_tree_wr_walk(struct ma_wr_state *wr_mas);
void rust_maple_tree_wr_end_piv(struct ma_wr_state *wr_mas);
void rust_maple_tree_wr_extend_null(struct ma_wr_state *wr_mas);
unsigned char rust_maple_tree_wr_new_end(struct ma_wr_state *wr_mas);
bool rust_maple_tree_in_rcu(struct maple_tree *mt);
unsigned char rust_maple_tree_min_slots(enum maple_type type);
unsigned char rust_maple_tree_slot_count(enum maple_type type);
unsigned int rust_maple_tree_height(struct ma_state *mas);
void rust_maple_tree_alloc_nodes(struct ma_state *mas, gfp_t gfp);
void rust_maple_tree_destroy(struct ma_state *mas);
int rust_maple_tree_error(struct ma_state *mas);

#endif /* CONFIG_RUST_MAPLE_TREE_PREALLOC */

#ifdef CONFIG_RUST_MAPLE_TREE_STORE_ENTRY

void rust_mas_wr_store_entry(struct ma_wr_state *wr_mas);

void rust_maple_tree_store_slot(void __rcu **slot, void *entry);
void *rust_maple_tree_store_slot_locked(struct ma_state *mas,
					void __rcu **slots,
					unsigned char offset);
void rust_maple_tree_store_set_meta(struct maple_node *node,
				    enum maple_type type,
				    unsigned char offset,
				    unsigned char end);
void rust_maple_tree_store_update_gap(struct ma_state *mas);
void rust_maple_tree_store_trace(struct ma_state *mas,
				 unsigned char offset, void *entry);
void rust_maple_tree_store_complex(struct ma_wr_state *wr_mas);

#endif /* CONFIG_RUST_MAPLE_TREE_STORE_ENTRY */

#ifdef CONFIG_RUST_MAPLE_TREE_NODE_STORE

void rust_maple_tree_store_trace_op(struct ma_state *mas);
struct maple_node *rust_maple_tree_store_pop_node(struct ma_state *mas);
void rust_maple_tree_store_set_parent(struct maple_node *dst,
				      const struct maple_node *src);
void rust_maple_tree_store_copy_node(struct maple_node *dst,
				     const struct maple_node *src);
void rust_maple_tree_store_replace_node(struct ma_state *mas,
					struct maple_node *node,
					enum maple_type type,
					unsigned int height);

#endif /* CONFIG_RUST_MAPLE_TREE_NODE_STORE */

#endif /* _LINUX_MAPLE_TREE_RUST_H */
