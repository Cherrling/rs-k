/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Internal interface for the native Rust XArray implementation.
 */

#ifndef _LINUX_XARRAY_RUST_H
#define _LINUX_XARRAY_RUST_H

#include <linux/xarray.h>

#ifdef CONFIG_RUST_XARRAY_LOAD

enum rust_xarray_constant {
	RUST_XARRAY_CHUNK_MASK = XA_CHUNK_MASK,
};

void *rust_xarray_load(struct xarray *xa, unsigned long index);
void *rust_xarray_load_advanced(struct xa_state *xas);

#endif /* CONFIG_RUST_XARRAY_LOAD */

#ifdef CONFIG_RUST_XARRAY_STORE

void *rust_xarray_store(struct xa_state *xas, void *entry);
void *rust_xarray_create_for_store(struct xa_state *xas, bool allow_root);
void rust_xarray_free_nodes(struct xa_state *xas, struct xa_node *top);
#ifndef CONFIG_RUST_XARRAY_DELETE_NODE
void rust_xarray_update_node(struct xa_state *xas, struct xa_node *node,
			     int count, int values);
#endif
void rust_xarray_squash_marks(const struct xa_state *xas);

#endif /* CONFIG_RUST_XARRAY_STORE */

#ifdef CONFIG_RUST_XARRAY_FREE_NODES

void rust_xarray_notify_node(struct xa_state *xas, struct xa_node *node);
void rust_xarray_retire_node(struct xa_node *node);

#endif /* CONFIG_RUST_XARRAY_FREE_NODES */

#if defined(CONFIG_RUST_XARRAY_MARKS) || \
	defined(CONFIG_RUST_XARRAY_DELETE_NODE)

enum rust_xarray_mark_constant {
	RUST_XARRAY_MARK_MAX = XA_MARK_MAX,
	RUST_XARRAY_FREE_MARK = XA_FREE_MARK,
};

#endif /* CONFIG_RUST_XARRAY_MARKS || CONFIG_RUST_XARRAY_DELETE_NODE */

#ifdef CONFIG_RUST_XARRAY_MARKS

bool rust_xarray_mark_get(const struct xa_state *xas, xa_mark_t mark);
void rust_xarray_mark_set(const struct xa_state *xas, xa_mark_t mark);
void rust_xarray_mark_clear(const struct xa_state *xas, xa_mark_t mark);
void rust_xarray_marks_init(const struct xa_state *xas);

#endif /* CONFIG_RUST_XARRAY_MARKS */

#endif /* _LINUX_XARRAY_RUST_H */
