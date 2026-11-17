/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Internal interface between the B+Tree core and the native Rust traversal.
 */

#ifndef _LINUX_BTREE_RUST_H
#define _LINUX_BTREE_RUST_H

#include <linux/btree.h>

#ifdef CONFIG_RUST_BTREE

void *rust_btree_lookup(struct btree_head *head, struct btree_geo *geo,
			unsigned long *key);
int rust_btree_update(struct btree_head *head, struct btree_geo *geo,
		      unsigned long *key, void *val);
void *rust_btree_last(struct btree_head *head, struct btree_geo *geo,
		      unsigned long *key);
void *rust_btree_get_prev(struct btree_head *head, struct btree_geo *geo,
			  unsigned long *key);

#endif /* CONFIG_RUST_BTREE */

#endif /* _LINUX_BTREE_RUST_H */
