/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Internal interface between klists and native Rust.
 */

#ifndef _LINUX_KLIST_RUST_H
#define _LINUX_KLIST_RUST_H

#include <linux/klist.h>

void klist_wake_waiters(struct klist_node *node);

#endif /* _LINUX_KLIST_RUST_H */
