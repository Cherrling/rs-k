/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Internal interface between the priority list core and native Rust.
 */

#ifndef _LINUX_PLIST_RUST_H
#define _LINUX_PLIST_RUST_H

#include <linux/plist.h>

#ifdef CONFIG_RUST_PLIST

bool rust_plist_list_empty(const struct list_head *list);
void rust_plist_list_add(struct list_head *new, struct list_head *head);
void rust_plist_list_add_tail(struct list_head *new, struct list_head *head);
void rust_plist_list_del_init(struct list_head *entry);

void rust_plist_add(struct plist_node *node, struct plist_head *head);
void rust_plist_del(struct plist_node *node, struct plist_head *head);
void rust_plist_requeue(struct plist_node *node, struct plist_head *head);

#endif /* CONFIG_RUST_PLIST */

#endif /* _LINUX_PLIST_RUST_H */
