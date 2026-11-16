// SPDX-License-Identifier: GPL-2.0

#include <linux/panic.h>
#include <linux/xarray.h>

__rust_helper int rust_helper_xa_err(void *entry)
{
	return xa_err(entry);
}

__rust_helper void rust_helper_xa_init_flags(struct xarray *xa, gfp_t flags)
{
	return xa_init_flags(xa, flags);
}

__rust_helper int rust_helper_xa_trylock(struct xarray *xa)
{
	return xa_trylock(xa);
}

__rust_helper void rust_helper_xa_lock(struct xarray *xa)
{
	return xa_lock(xa);
}

__rust_helper void rust_helper_xa_unlock(struct xarray *xa)
{
	return xa_unlock(xa);
}

#ifdef CONFIG_RUST_XARRAY_LOAD
__rust_helper void *rust_helper_xarray_head(const struct xarray *xa)
{
	return xa_head(xa);
}

__rust_helper void *rust_helper_xarray_entry(const struct xarray *xa,
					     const struct xa_node *node,
					     unsigned int offset)
{
	return xa_entry(xa, node, offset);
}

__rust_helper bool rust_helper_xarray_is_node(const void *entry)
{
	return xa_is_node(entry);
}

__rust_helper struct xa_node *
rust_helper_xarray_to_node(const void *entry)
{
	return xa_to_node(entry);
}

__rust_helper bool rust_helper_xarray_is_sibling(const void *entry)
{
	return xa_is_sibling(entry);
}

__rust_helper unsigned long
rust_helper_xarray_to_sibling(const void *entry)
{
	return xa_to_sibling(entry);
}

__rust_helper bool rust_helper_xarray_is_retry(const void *entry)
{
	return xa_is_retry(entry);
}

__rust_helper bool rust_helper_xarray_is_zero(const void *entry)
{
	return xa_is_zero(entry);
}

__rust_helper void *rust_helper_xarray_retry_entry(void)
{
	return XA_RETRY_ENTRY;
}

__rust_helper void rust_helper_xarray_set_bounds(struct xa_state *xas)
{
	xas->xa_node = XAS_BOUNDS;
}

__rust_helper void rust_helper_xarray_reset(struct xa_state *xas)
{
	xas_reset(xas);
}

__rust_helper int rust_helper_xarray_error(const struct xa_state *xas)
{
	return xas_error(xas);
}

__rust_helper void rust_helper_xarray_state_init(struct xa_state *xas,
						 struct xarray *xa,
						 unsigned long index)
{
	*xas = (struct xa_state)__XA_STATE(xa, index, 0, 0);
}
#endif

#ifdef CONFIG_RUST_XARRAY_STORE
__rust_helper bool rust_helper_xarray_is_value(const void *entry)
{
	return xa_is_value(entry);
}

__rust_helper void *rust_helper_xarray_mk_sibling(unsigned int offset)
{
	return xa_mk_sibling(offset);
}

__rust_helper void *rust_helper_xarray_slot_locked(const struct xa_state *xas,
						   const struct xa_node *node,
						   unsigned int offset)
{
	if (node)
		return xa_entry_locked(xas->xa, node, offset);
	return xa_head_locked(xas->xa);
}

__rust_helper void rust_helper_xarray_assign_slot(struct xa_state *xas,
						  struct xa_node *node,
						  unsigned int offset,
						  void *entry)
{
	if (node)
		rcu_assign_pointer(node->slots[offset], entry);
	else
		rcu_assign_pointer(xas->xa->xa_head, entry);
}
#endif

#if defined(CONFIG_RUST_XARRAY_MARKS) || \
	defined(CONFIG_RUST_XARRAY_DELETE_NODE) || \
	defined(CONFIG_RUST_XARRAY_EXPAND)
__rust_helper bool rust_helper_xarray_node_mark_get(const struct xa_node *node,
						    unsigned int offset,
						    xa_mark_t mark)
{
	return test_bit(offset, node->marks[(__force unsigned int)mark]);
}

__rust_helper bool
rust_helper_xarray_node_mark_test_and_set(struct xa_node *node,
					  unsigned int offset,
					  xa_mark_t mark)
{
	return __test_and_set_bit(offset,
				  node->marks[(__force unsigned int)mark]);
}

__rust_helper bool
rust_helper_xarray_node_mark_test_and_clear(struct xa_node *node,
					    unsigned int offset,
					    xa_mark_t mark)
{
	return __test_and_clear_bit(offset,
				    node->marks[(__force unsigned int)mark]);
}

__rust_helper bool rust_helper_xarray_node_any_mark(const struct xa_node *node,
						    xa_mark_t mark)
{
	return !bitmap_empty(node->marks[(__force unsigned int)mark],
			     XA_CHUNK_SIZE);
}

__rust_helper bool rust_helper_xarray_root_marked(const struct xarray *xa,
						  xa_mark_t mark)
{
	return xa_marked(xa, mark);
}

__rust_helper void rust_helper_xarray_root_mark_set(struct xarray *xa,
						    xa_mark_t mark)
{
	xa->xa_flags |= XA_FLAGS_MARK(mark);
}

__rust_helper void rust_helper_xarray_root_mark_clear(struct xarray *xa,
						      xa_mark_t mark)
{
	xa->xa_flags &= ~XA_FLAGS_MARK(mark);
}

__rust_helper bool rust_helper_xarray_track_free(const struct xarray *xa)
{
	return xa->xa_flags & XA_FLAGS_TRACK_FREE;
}
#endif

#ifdef CONFIG_RUST_XARRAY_EXPAND
__rust_helper void *rust_helper_xarray_mk_node(const struct xa_node *node)
{
	return xa_mk_node(node);
}

__rust_helper void
rust_helper_xarray_node_slot_zero_init(struct xa_node *node, void *entry)
{
	RCU_INIT_POINTER(node->slots[0], entry);
}

__rust_helper void
rust_helper_xarray_node_mark_all(struct xa_node *node, xa_mark_t mark)
{
	bitmap_fill(node->marks[(__force unsigned int)mark], XA_CHUNK_SIZE);
}

__rust_helper void
rust_helper_xarray_link_root_child(struct xa_node *child,
				   struct xa_node *parent)
{
	child->offset = 0;
	rcu_assign_pointer(child->parent, parent);
}

__rust_helper bool rust_helper_xarray_expand_check(struct xa_state *xas,
						   unsigned int shift)
{
	if (WARN_ON_ONCE(shift > BITS_PER_LONG)) {
		xas_set_err(xas, -EINVAL);
		return false;
	}

	return true;
}
#endif

#if defined(CONFIG_RUST_XARRAY_MARKS) || \
	defined(CONFIG_RUST_XARRAY_FREE_NODES) || \
	defined(CONFIG_RUST_XARRAY_DELETE_NODE) || \
	defined(CONFIG_RUST_XARRAY_CREATE_RANGE) || \
	defined(CONFIG_RUST_XARRAY_FIND_CONFLICT)

__rust_helper struct xa_node *
rust_helper_xarray_parent_locked(const struct xarray *xa,
				 const struct xa_node *node)
{
	return xa_parent_locked(xa, node);
}
#endif

#if defined(CONFIG_RUST_XARRAY_FIND) || \
	defined(CONFIG_RUST_XARRAY_NEXT_PREV)
__rust_helper struct xa_node *
rust_helper_xarray_parent(const struct xarray *xa,
			  const struct xa_node *node)
{
	return xa_parent(xa, node);
}
#endif

#ifdef CONFIG_RUST_XARRAY_NEXT_PREV
__rust_helper bool rust_helper_xarray_frozen(struct xa_node *node)
{
	return xas_frozen(node);
}

__rust_helper bool rust_helper_xarray_not_node(struct xa_node *node)
{
	return xas_not_node(node);
}
#endif

#if defined(CONFIG_RUST_XARRAY_FIND_MARKED) || \
	defined(CONFIG_RUST_XARRAY_FIND_CONFLICT)
__rust_helper bool rust_helper_xarray_top(struct xa_node *node)
{
	return xas_top(node);
}
#endif

#if defined(CONFIG_RUST_XARRAY_FIND_MARKED) || \
	defined(CONFIG_RUST_XARRAY_PAUSE)
__rust_helper void rust_helper_xarray_set_restart(struct xa_state *xas)
{
	xas->xa_node = XAS_RESTART;
}
#endif

#ifdef CONFIG_RUST_XARRAY_PAUSE
__rust_helper bool rust_helper_xarray_invalid(const struct xa_state *xas)
{
	return xas_invalid(xas);
}
#endif

#ifdef CONFIG_RUST_XARRAY_FIND_MARKED
__rust_helper unsigned int
rust_helper_xarray_node_find_mark(const struct xa_node *node,
				  unsigned int offset, xa_mark_t mark)
{
	return find_next_bit(node->marks[(__force unsigned int)mark],
			     XA_CHUNK_SIZE, offset);
}
#endif

#ifdef CONFIG_RUST_XARRAY_FIND_CONFLICT
__rust_helper void *
rust_helper_xarray_entry_locked(const struct xarray *xa,
				const struct xa_node *node,
				unsigned int offset)
{
	return xa_entry_locked(xa, node, offset);
}
#endif

#if defined(CONFIG_RUST_XARRAY_DELETE_NODE) || \
	defined(CONFIG_RUST_XARRAY_CREATE)
__rust_helper bool rust_helper_xarray_zero_busy(const struct xarray *xa)
{
	return xa->xa_flags & XA_FLAGS_ZERO_BUSY;
}
#endif

#ifdef CONFIG_RUST_XARRAY_DELETE_NODE
__rust_helper void rust_helper_xarray_node_check(const struct xa_node *node)
{
	(void)node;
#ifdef XA_DEBUG
	if (unlikely(node->count > XA_CHUNK_SIZE ||
		     node->nr_values > XA_CHUNK_SIZE)) {
		xa_dump_node(node);
		panic("Corrupt XArray node accounting\n");
	}
#endif
}

__rust_helper void rust_helper_xarray_clear_parent(struct xa_node *node)
{
	node->parent = NULL;
}
#endif

#ifdef CONFIG_RUST_XARRAY_CREATE
__rust_helper void *rust_helper_xarray_zero_entry(void)
{
	return XA_ZERO_ENTRY;
}
#endif

#ifdef CONFIG_RUST_XARRAY_FREE_NODES
__rust_helper void rust_helper_xarray_slot_set_retry(struct xa_node *node,
						     unsigned int offset)
{
	RCU_INIT_POINTER(node->slots[offset], XA_RETRY_ENTRY);
}
#endif

#ifdef CONFIG_RUST_XARRAY_SQUASH_MARKS
__rust_helper unsigned int
rust_helper_xarray_node_mark_find_next(const struct xa_node *node,
				       xa_mark_t mark, unsigned int limit,
				       unsigned int start)
{
	return find_next_bit(node->marks[(__force unsigned int)mark],
			     limit, start);
}

__rust_helper void rust_helper_xarray_node_mark_set(struct xa_node *node,
						    xa_mark_t mark,
						    unsigned int offset)
{
	__set_bit(offset, node->marks[(__force unsigned int)mark]);
}

__rust_helper void rust_helper_xarray_node_mark_clear(struct xa_node *node,
						      xa_mark_t mark,
						      unsigned int start,
						      unsigned int nbits)
{
	bitmap_clear(node->marks[(__force unsigned int)mark], start, nbits);
}
#endif
