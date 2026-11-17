/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Internal helpers for the native Rust lockref implementation.
 */

#ifndef _LINUX_LOCKREF_RUST_H
#define _LINUX_LOCKREF_RUST_H

#include <linux/lockref.h>
#include <linux/types.h>

#ifdef CONFIG_RUST_LOCKREF

void rust_lockref_get(struct lockref *lockref);
bool rust_lockref_get_not_zero(struct lockref *lockref);
int rust_lockref_put_return(struct lockref *lockref);
bool rust_lockref_put_or_lock(struct lockref *lockref);
void rust_lockref_mark_dead(struct lockref *lockref);
bool rust_lockref_get_not_dead(struct lockref *lockref);

u64 rust_lockref_read_raw(const struct lockref *lockref);
int rust_lockref_raw_count(u64 raw);
bool rust_lockref_raw_unlocked(u64 raw);
bool rust_lockref_try_update(struct lockref *lockref, u64 *old, int count);

void rust_lockref_lock(struct lockref *lockref);
void rust_lockref_unlock(struct lockref *lockref);
int rust_lockref_locked_read(const struct lockref *lockref);
void rust_lockref_locked_write(struct lockref *lockref, int count);
void rust_lockref_mark_dead_under_lock(struct lockref *lockref, int count);

#endif /* CONFIG_RUST_LOCKREF */

#endif /* _LINUX_LOCKREF_RUST_H */
