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
void rust_xarray_update_node(struct xa_state *xas, struct xa_node *node,
			     int count, int values);
void rust_xarray_squash_marks(const struct xa_state *xas);

#endif /* CONFIG_RUST_XARRAY_STORE */

#endif /* _LINUX_XARRAY_RUST_H */
