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
	RUST_XARRAY_CHUNK_SHIFT = XA_CHUNK_SHIFT,
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

#if defined(CONFIG_RUST_XARRAY_FREE_NODES) || \
	defined(CONFIG_RUST_XARRAY_EXPAND)

void rust_xarray_notify_node(struct xa_state *xas, struct xa_node *node);

#endif /* CONFIG_RUST_XARRAY_FREE_NODES || CONFIG_RUST_XARRAY_EXPAND */

#ifdef CONFIG_RUST_XARRAY_FREE_NODES

void rust_xarray_retire_node(struct xa_node *node);

#endif /* CONFIG_RUST_XARRAY_FREE_NODES */

#ifdef CONFIG_RUST_XARRAY_EXPAND

enum rust_xarray_expand_constant {
	RUST_XARRAY_EINVAL = EINVAL,
	RUST_XARRAY_ENOMEM = ENOMEM,
};

int rust_xarray_expand(struct xa_state *xas, void *head);
struct xa_node *rust_xarray_alloc_node(struct xa_state *xas,
				       unsigned int shift);

#endif /* CONFIG_RUST_XARRAY_EXPAND */

#if defined(CONFIG_RUST_XARRAY_MARKS) || \
	defined(CONFIG_RUST_XARRAY_DELETE_NODE) || \
	defined(CONFIG_RUST_XARRAY_EXPAND)

enum rust_xarray_mark_constant {
	RUST_XARRAY_MARK_MAX = XA_MARK_MAX,
	RUST_XARRAY_FREE_MARK = XA_FREE_MARK,
};

#endif /* Rust XArray mark users */

#ifdef CONFIG_RUST_XARRAY_MARKS

bool rust_xarray_mark_get(const struct xa_state *xas, xa_mark_t mark);
void rust_xarray_mark_set(const struct xa_state *xas, xa_mark_t mark);
void rust_xarray_mark_clear(const struct xa_state *xas, xa_mark_t mark);
void rust_xarray_marks_init(const struct xa_state *xas);

#endif /* CONFIG_RUST_XARRAY_MARKS */

#endif /* _LINUX_XARRAY_RUST_H */
