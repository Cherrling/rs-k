// SPDX-License-Identifier: GPL-2.0

#include <linux/atomic.h>
#include <linux/generic-radix-tree.h>

__rust_helper struct genradix_root *
rust_helper_genradix_root_load(struct __genradix *radix)
{
	return READ_ONCE(radix->root);
}

__rust_helper struct genradix_root *
rust_helper_genradix_root_cmpxchg(struct __genradix *radix,
				  struct genradix_root *old,
				  struct genradix_root *new)
{
	return cmpxchg_release(&radix->root, old, new);
}

__rust_helper struct genradix_root *
rust_helper_genradix_root_xchg_null(struct __genradix *radix)
{
	return xchg(&radix->root, NULL);
}

__rust_helper struct genradix_node *
rust_helper_genradix_root_to_node(struct genradix_root *root)
{
	return genradix_root_to_node(root);
}

__rust_helper unsigned int
rust_helper_genradix_root_to_depth(struct genradix_root *root)
{
	return genradix_root_to_depth(root);
}

__rust_helper struct genradix_root *
rust_helper_genradix_make_root(struct genradix_node *node,
			       unsigned int depth)
{
	return (struct genradix_root *)((unsigned long)node | depth);
}

__rust_helper struct genradix_node *
rust_helper_genradix_node_alloc(gfp_t gfp_mask)
{
	return genradix_alloc_node(gfp_mask);
}

__rust_helper void rust_helper_genradix_node_free(struct genradix_node *node)
{
	genradix_free_node(node);
}

__rust_helper struct genradix_node *
rust_helper_genradix_node_child_load(struct genradix_node *node,
				     size_t index)
{
	return READ_ONCE(node->children[index]);
}

__rust_helper void
rust_helper_genradix_node_child_set(struct genradix_node *node,
				    size_t index,
				    struct genradix_node *child)
{
	node->children[index] = child;
}

__rust_helper struct genradix_node *
rust_helper_genradix_node_child_install(struct genradix_node *node,
					size_t index,
					struct genradix_node *child)
{
	return cmpxchg_release(&node->children[index], NULL, child);
}

__rust_helper void *
rust_helper_genradix_node_data(struct genradix_node *node)
{
	return node->data;
}
