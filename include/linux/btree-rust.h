/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Internal interface between the B+Tree core and the native Rust traversal.
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

void *rust_btree_lookup(struct btree_head *head, struct btree_geo *geo,
			unsigned long *key);
int rust_btree_update(struct btree_head *head, struct btree_geo *geo,
		      unsigned long *key, void *val);
int rust_btree_insert(struct btree_head *head, struct btree_geo *geo,
		      unsigned long *key, void *val, gfp_t gfp);
void *rust_btree_remove(struct btree_head *head, struct btree_geo *geo,
			unsigned long *key);
int rust_btree_merge(struct btree_head *target, struct btree_head *victim,
		     struct btree_geo *geo, gfp_t gfp);
void *rust_btree_last(struct btree_head *head, struct btree_geo *geo,
		      unsigned long *key);
void *rust_btree_get_prev(struct btree_head *head, struct btree_geo *geo,
			  unsigned long *key);
size_t rust_btree_visitor(struct btree_head *head, struct btree_geo *geo,
			  unsigned long opaque, btree_rust_visit_fn func,
			  void *func2);
size_t rust_btree_grim_visitor(struct btree_head *head, struct btree_geo *geo,
			       unsigned long opaque, btree_rust_visit_fn func,
			       void *func2);

#endif /* CONFIG_RUST_BTREE */

#endif /* _LINUX_BTREE_RUST_H */
