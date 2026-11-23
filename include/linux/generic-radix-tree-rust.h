/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Constants shared with the native Rust generic radix tree implementation.
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

#endif /* CONFIG_RUST_GENRADIX */

#endif /* _LINUX_GENERIC_RADIX_TREE_RUST_H */
