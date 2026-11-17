/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Internal interface between the interval tree and native Rust.
 */

#ifndef _LINUX_INTERVAL_TREE_RUST_H
#define _LINUX_INTERVAL_TREE_RUST_H

#include <linux/interval_tree.h>

#ifdef CONFIG_RUST_INTERVAL_TREE

void rust_interval_tree_link_insert(struct interval_tree_node *node,
				    struct rb_root_cached *root,
				    struct rb_node *parent,
				    struct rb_node **link, bool leftmost);
void rust_interval_tree_erase(struct interval_tree_node *node,
			      struct rb_root_cached *root);

void rust_interval_tree_insert(struct interval_tree_node *node,
			       struct rb_root_cached *root);
void rust_interval_tree_remove(struct interval_tree_node *node,
			       struct rb_root_cached *root);
struct interval_tree_node *
rust_interval_tree_subtree_search(struct interval_tree_node *node,
				  unsigned long start, unsigned long last);
struct interval_tree_node *
rust_interval_tree_iter_first(struct rb_root_cached *root,
			      unsigned long start, unsigned long last);
struct interval_tree_node *
rust_interval_tree_iter_next(struct interval_tree_node *node,
			     unsigned long start, unsigned long last);

#ifdef CONFIG_INTERVAL_TREE_SPAN_ITER

void rust_interval_tree_span_iter_first(struct interval_tree_span_iter *iter,
					struct rb_root_cached *tree,
					unsigned long first,
					unsigned long last);
void rust_interval_tree_span_iter_next(struct interval_tree_span_iter *iter);
void rust_interval_tree_span_iter_advance(struct interval_tree_span_iter *iter,
					  struct rb_root_cached *tree,
					  unsigned long index);

#endif /* CONFIG_INTERVAL_TREE_SPAN_ITER */

#endif /* CONFIG_RUST_INTERVAL_TREE */

#endif /* _LINUX_INTERVAL_TREE_RUST_H */
