/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Internal helpers for the native Rust decrement-and-lock protocol.
 */

#ifndef _LINUX_DEC_AND_LOCK_RUST_H
#define _LINUX_DEC_AND_LOCK_RUST_H

#include <linux/spinlock_types.h>
#include <linux/types.h>

struct mutex;

#ifdef CONFIG_RUST_DEC_AND_LOCK

int rust_dec_and_lock(atomic_t *atomic, spinlock_t *lock);
int rust_dec_and_lock_irqsave(atomic_t *atomic, spinlock_t *lock,
			      unsigned long *flags);
int rust_dec_and_raw_lock(atomic_t *atomic, raw_spinlock_t *lock);
int rust_dec_and_raw_lock_irqsave(atomic_t *atomic, raw_spinlock_t *lock,
				  unsigned long *flags);
int rust_dec_and_mutex_lock(atomic_t *atomic, struct mutex *lock);

int rust_dec_and_lock_atomic_add_unless(atomic_t *atomic);
bool rust_dec_and_lock_atomic_dec_and_test(atomic_t *atomic);

void rust_dec_and_lock_spin_lock(spinlock_t *lock);
void rust_dec_and_lock_spin_unlock(spinlock_t *lock);
void rust_dec_and_lock_spin_lock_irqsave(spinlock_t *lock,
					 unsigned long *flags);
void rust_dec_and_lock_spin_unlock_irqrestore(spinlock_t *lock,
					      unsigned long flags);

void rust_dec_and_lock_raw_lock(raw_spinlock_t *lock);
void rust_dec_and_lock_raw_unlock(raw_spinlock_t *lock);
void rust_dec_and_lock_raw_lock_irqsave(raw_spinlock_t *lock,
					unsigned long *flags);
void rust_dec_and_lock_raw_unlock_irqrestore(raw_spinlock_t *lock,
					     unsigned long flags);

void rust_dec_and_lock_mutex_lock(struct mutex *lock);
void rust_dec_and_lock_mutex_unlock(struct mutex *lock);

#endif /* CONFIG_RUST_DEC_AND_LOCK */

#endif /* _LINUX_DEC_AND_LOCK_RUST_H */
