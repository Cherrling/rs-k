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

#endif /* CONFIG_RUST_INTERVAL_TREE */

#endif /* _LINUX_INTERVAL_TREE_RUST_H */
