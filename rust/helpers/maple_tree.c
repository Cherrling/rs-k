// SPDX-License-Identifier: GPL-2.0

#include <linux/maple_tree.h>
#include <linux/maple_tree_rust.h>

__rust_helper void rust_helper_mt_init_flags(struct maple_tree *mt,
					     unsigned int flags)
{
	mt_init_flags(mt, flags);
}
