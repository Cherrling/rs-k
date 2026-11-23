/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Shared callback ABI and node allocation helpers for the native Rust B+Tree
 * implementation.
 */

#ifndef _LINUX_BTREE_RUST_H
#define _LINUX_BTREE_RUST_H

#include <linux/btree.h>

typedef void (*btree_rust_visit_fn)(void *elem, unsigned long opaque,
				    unsigned long *key, size_t index,
				    void *func2);

#ifdef CONFIG_RUST_BTREE

unsigned long *btree_rust_node_alloc(struct btree_head *head, gfp_t gfp);
void btree_rust_node_free(struct btree_head *head, unsigned long *node);

#endif /* CONFIG_RUST_BTREE */

#endif /* _LINUX_BTREE_RUST_H */
