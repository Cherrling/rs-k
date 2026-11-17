/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Internal interface between klists and native Rust.
 */

#ifndef _LINUX_KLIST_RUST_H
#define _LINUX_KLIST_RUST_H

#include <linux/klist.h>

void klist_wake_waiters(struct klist_node *node);

#ifdef CONFIG_RUST_KLIST

void rust_klist_add_head(struct klist_node *n, struct klist *k);
void rust_klist_add_tail(struct klist_node *n, struct klist *k);
void rust_klist_add_behind(struct klist_node *n, struct klist_node *pos);
void rust_klist_add_before(struct klist_node *n, struct klist_node *pos);
void rust_klist_del(struct klist_node *n);
int rust_klist_node_attached(struct klist_node *n);
void rust_klist_iter_init(struct klist *k, struct klist_iter *i);
void rust_klist_iter_init_node(struct klist *k, struct klist_iter *i,
			       struct klist_node *n);
void rust_klist_iter_exit(struct klist_iter *i);
struct klist_node *rust_klist_prev(struct klist_iter *i);
struct klist_node *rust_klist_next(struct klist_iter *i);

#endif /* CONFIG_RUST_KLIST */

#endif /* _LINUX_KLIST_RUST_H */
