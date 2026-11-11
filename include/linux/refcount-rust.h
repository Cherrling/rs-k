/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Internal helpers for the native Rust refcount_t implementation.
 */

#ifndef _LINUX_REFCOUNT_RUST_H
#define _LINUX_REFCOUNT_RUST_H

#include <linux/refcount_types.h>
#include <linux/spinlock_types.h>

struct mutex;

#ifdef CONFIG_RUST_REFCOUNT

bool rust_refcount_add_not_zero(int i, refcount_t *r, int *oldp);
bool rust_refcount_add_not_zero_limited_acquire(int i, refcount_t *r,
						int *oldp, int limit);
void rust_refcount_add(int i, refcount_t *r, int *oldp);
bool rust_refcount_sub_and_test(int i, refcount_t *r, int *oldp);
void rust_refcount_dec(refcount_t *r, int *oldp);
void rust_refcount_warn_saturate(refcount_t *r, int type);

bool rust_refcount_dec_if_one(refcount_t *r);
bool rust_refcount_dec_not_one(refcount_t *r);
bool rust_refcount_dec_and_mutex_lock(refcount_t *r, struct mutex *lock);
bool rust_refcount_dec_and_lock(refcount_t *r, spinlock_t *lock);
bool rust_refcount_dec_and_lock_irqsave(refcount_t *r, spinlock_t *lock,
					unsigned long *flags);

int rust_refcount_atomic_read(const refcount_t *r);
void rust_refcount_atomic_set(refcount_t *r, int value);
bool rust_refcount_try_cmpxchg_relaxed(refcount_t *r, int *old, int new);
bool rust_refcount_try_cmpxchg_acquire(refcount_t *r, int *old, int new);
bool rust_refcount_try_cmpxchg_release(refcount_t *r, int *old, int new);
int rust_refcount_fetch_add_relaxed(int i, refcount_t *r);
int rust_refcount_fetch_sub_release(int i, refcount_t *r);
void rust_refcount_acquire_after_ctrl_dep(void);

void rust_refcount_mutex_lock(struct mutex *lock);
void rust_refcount_mutex_unlock(struct mutex *lock);
void rust_refcount_spin_lock(spinlock_t *lock);
void rust_refcount_spin_unlock(spinlock_t *lock);
void rust_refcount_spin_lock_irqsave(spinlock_t *lock, unsigned long *flags);
void rust_refcount_spin_unlock_irqrestore(spinlock_t *lock,
					  unsigned long flags);

void rust_refcount_warn(int type);
void rust_refcount_warn_dec_not_one(void);

#endif /* CONFIG_RUST_REFCOUNT */

#endif /* _LINUX_REFCOUNT_RUST_H */
