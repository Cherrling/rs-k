/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Internal interface for the native Rust generic radix tree implementation.
 */

#ifndef _LINUX_GENERIC_RADIX_TREE_RUST_H
#define _LINUX_GENERIC_RADIX_TREE_RUST_H

#include <linux/generic-radix-tree.h>

#ifdef CONFIG_RUST_GENRADIX

enum rust_genradix_constant {
	RUST_GENRADIX_NODE_SHIFT = GENRADIX_NODE_SHIFT,
	RUST_GENRADIX_NODE_SIZE = GENRADIX_NODE_SIZE,
	RUST_GENRADIX_ARY = GENRADIX_ARY,
	RUST_GENRADIX_ARY_SHIFT = GENRADIX_ARY_SHIFT,
};

void *rust_genradix_ptr(struct __genradix *radix, size_t offset);
void *rust_genradix_ptr_alloc(struct __genradix *radix, size_t offset,
			      struct genradix_node **preallocated,
			      gfp_t gfp_mask);
void *rust_genradix_iter_peek(struct genradix_iter *iter,
			      struct __genradix *radix,
			      size_t objs_per_page);
void *rust_genradix_iter_peek_prev(struct genradix_iter *iter,
				   struct __genradix *radix,
				   size_t objs_per_page,
				   size_t obj_size_plus_page_remainder);
int rust_genradix_prealloc(struct __genradix *radix, size_t size,
			   gfp_t gfp_mask);
void rust_genradix_free(struct __genradix *radix);

#endif /* CONFIG_RUST_GENRADIX */

#endif /* _LINUX_GENERIC_RADIX_TREE_RUST_H */
