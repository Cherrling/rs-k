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

#ifdef CONFIG_RUST_XARRAY_NOMEM

enum rust_xarray_nomem_constant {
	RUST_XARRAY_NOMEM_ENOMEM = ENOMEM,
};

bool rust_xarray_nomem(struct xa_state *xas, gfp_t gfp);
void rust_xarray_destroy(struct xa_state *xas);

struct xa_node *rust_xarray_alloc_prealloc(struct xa_state *xas, gfp_t gfp);
struct xa_node *rust_xarray_parent_prealloc(const struct xa_node *node);
void rust_xarray_prepare_prealloc(struct xa_node *node);
void rust_xarray_free_prealloc(struct xa_node *node);

#endif /* CONFIG_RUST_XARRAY_NOMEM */

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

#ifdef CONFIG_RUST_XARRAY_CREATE

void *rust_xarray_create(struct xa_state *xas, bool allow_root);

#endif /* CONFIG_RUST_XARRAY_CREATE */

#ifdef CONFIG_RUST_XARRAY_CREATE_RANGE

void rust_xarray_create_range(struct xa_state *xas);

#endif /* CONFIG_RUST_XARRAY_CREATE_RANGE */

#ifdef CONFIG_RUST_XARRAY_RANGE_STORE

enum rust_xarray_range_store_constant {
	RUST_XARRAY_RANGE_STORE_EINVAL = -EINVAL,
};

void *rust_xarray_store_range(struct xarray *xa, unsigned long first,
			      unsigned long last, void *entry, gfp_t gfp);
int rust_xarray_xas_get_order(struct xa_state *xas);
int rust_xarray_get_order(struct xarray *xa, unsigned long index);

#endif /* CONFIG_RUST_XARRAY_RANGE_STORE */

#ifdef CONFIG_RUST_XARRAY_FIND

void *rust_xarray_find(struct xa_state *xas, unsigned long max);

#endif /* CONFIG_RUST_XARRAY_FIND */

#ifdef CONFIG_RUST_XARRAY_FIND_CONFLICT

void *rust_xarray_find_conflict(struct xa_state *xas);

#endif /* CONFIG_RUST_XARRAY_FIND_CONFLICT */

#ifdef CONFIG_RUST_XARRAY_FIND_MARKED

void *rust_xarray_find_marked(struct xa_state *xas, unsigned long max, xa_mark_t mark);

#endif /* CONFIG_RUST_XARRAY_FIND_MARKED */

#ifdef CONFIG_RUST_XARRAY_NEXT_PREV

void *rust_xarray_prev(struct xa_state *xas);
void *rust_xarray_next(struct xa_state *xas);

#endif /* CONFIG_RUST_XARRAY_NEXT_PREV */

#ifdef CONFIG_RUST_XARRAY_PAUSE

void rust_xarray_pause(struct xa_state *xas);

#endif /* CONFIG_RUST_XARRAY_PAUSE */

#ifdef CONFIG_RUST_XARRAY_SPLIT

void rust_xarray_split_alloc(struct xa_state *xas, void *entry, unsigned int order, gfp_t gfp);
void rust_xarray_split(struct xa_state *xas, void *entry, unsigned int order);

#endif /* CONFIG_RUST_XARRAY_SPLIT */

#ifdef CONFIG_RUST_XARRAY_TRY_SPLIT

unsigned int rust_xarray_try_split_min_order(unsigned int order);
void rust_xarray_try_split(struct xa_state *xas, void *entry, unsigned int order);

#endif /* CONFIG_RUST_XARRAY_TRY_SPLIT */

#ifdef CONFIG_RUST_XARRAY_DESTROY

void rust_xarray_destroy_tree(struct xa_state *xas);

#endif /* CONFIG_RUST_XARRAY_DESTROY */

#ifdef CONFIG_RUST_XARRAY_ALLOC

enum rust_xarray_alloc_constant {
	RUST_XARRAY_ALLOC_EINVAL = -EINVAL,
	RUST_XARRAY_ALLOC_EBUSY = -EBUSY,
};

bool rust_xarray_alloc_nomem(struct xa_state *xas, gfp_t gfp);

#endif /* CONFIG_RUST_XARRAY_ALLOC */

#if defined(CONFIG_RUST_XARRAY_FREE_NODES) || \
	defined(CONFIG_RUST_XARRAY_EXPAND) || \
	defined(CONFIG_RUST_XARRAY_SPLIT)

void rust_xarray_notify_node(struct xa_state *xas, struct xa_node *node);

#endif /* Rust XArray node update users */

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
