/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Queue read/write lock
 *
 * These use generic atomic and locking routines, but depend on a fair spinlock
 * implementation in order to be fair themselves.  The implementation in
 * asm-generic/spinlock.h meets these requirements.
 *
 * (C) Copyright 2013-2014 Hewlett-Packard Development Company, L.P.
 *
 * Authors: Waiman Long <waiman.long@hp.com>
 */
#ifndef __ASM_GENERIC_QRWLOCK_H
#define __ASM_GENERIC_QRWLOCK_H

#include <linux/atomic.h>
#include <asm/barrier.h>
#include <asm/processor.h>

#include <asm-generic/qrwlock_types.h>
#include <linux/rwlock-rust.h>

/* Must be included from asm/spinlock.h after defining arch_spin_is_locked.  */

/*
 * External function declarations
 */
extern void queued_read_lock_slowpath(struct qrwlock *lock);
extern void queued_write_lock_slowpath(struct qrwlock *lock);

/**
 * queued_read_trylock - try to acquire read lock of a queued rwlock
 * @lock : Pointer to queued rwlock structure
 * Return: 1 if lock acquired, 0 if failed
 */
static inline int queued_read_trylock(struct qrwlock *lock)
{
#ifdef CONFIG_RUST_RWLOCK
	return rust_qrwlock_read_trylock(lock);
#else
	int cnts;

	cnts = atomic_read(&lock->cnts);
	if (likely(!(cnts & QRW_WMASK))) {
		cnts = (u32)atomic_add_return_acquire(QRW_BIAS, &lock->cnts);
		if (likely(!(cnts & QRW_WMASK)))
			return 1;
		atomic_sub(QRW_BIAS, &lock->cnts);
	}
	return 0;
#endif
}

/**
 * queued_write_trylock - try to acquire write lock of a queued rwlock
 * @lock : Pointer to queued rwlock structure
 * Return: 1 if lock acquired, 0 if failed
 */
static inline int queued_write_trylock(struct qrwlock *lock)
{
#ifdef CONFIG_RUST_RWLOCK
	return rust_qrwlock_write_trylock(lock);
#else
	int cnts;

	cnts = atomic_read(&lock->cnts);
	if (unlikely(cnts))
		return 0;

	return likely(atomic_try_cmpxchg_acquire(&lock->cnts, &cnts,
				QRW_LOCKED));
#endif
}
/**
 * queued_read_lock - acquire read lock of a queued rwlock
 * @lock: Pointer to queued rwlock structure
 */
static inline void queued_read_lock(struct qrwlock *lock)
{
#ifdef CONFIG_RUST_RWLOCK
	if (!rust_qrwlock_read_lock(lock))
		queued_read_lock_slowpath(lock);
#else
	int cnts;

	cnts = atomic_add_return_acquire(QRW_BIAS, &lock->cnts);
	if (likely(!(cnts & QRW_WMASK)))
		return;

	/* The slowpath will decrement the reader count, if necessary. */
	queued_read_lock_slowpath(lock);
#endif
}

/**
 * queued_write_lock - acquire write lock of a queued rwlock
 * @lock : Pointer to queued rwlock structure
 */
static inline void queued_write_lock(struct qrwlock *lock)
{
#ifdef CONFIG_RUST_RWLOCK
	if (!rust_qrwlock_write_lock(lock))
		queued_write_lock_slowpath(lock);
#else
	int cnts = 0;
	/* Optimize for the unfair lock case where the fair flag is 0. */
	if (likely(atomic_try_cmpxchg_acquire(&lock->cnts, &cnts, QRW_LOCKED)))
		return;

	queued_write_lock_slowpath(lock);
#endif
}

/**
 * queued_read_unlock - release read lock of a queued rwlock
 * @lock : Pointer to queued rwlock structure
 */
static inline void queued_read_unlock(struct qrwlock *lock)
{
#ifdef CONFIG_RUST_RWLOCK
	rust_qrwlock_read_unlock(lock);
#else
	/*
	 * Atomically decrement the reader count
	 */
	(void)atomic_sub_return_release(QRW_BIAS, &lock->cnts);
#endif
}

/**
 * queued_write_unlock - release write lock of a queued rwlock
 * @lock : Pointer to queued rwlock structure
 */
static inline void queued_write_unlock(struct qrwlock *lock)
{
#ifdef CONFIG_RUST_RWLOCK
	rust_qrwlock_write_unlock(lock);
#else
	smp_store_release(&lock->wlocked, 0);
#endif
}

/**
 * queued_rwlock_is_contended - check if the lock is contended
 * @lock : Pointer to queued rwlock structure
 * Return: 1 if lock contended, 0 otherwise
 */
static inline int queued_rwlock_is_contended(struct qrwlock *lock)
{
	return arch_spin_is_locked(&lock->wait_lock);
}

/*
 * Remapping rwlock architecture specific functions to the corresponding
 * queued rwlock functions.
 */
#define arch_read_lock(l)		queued_read_lock(l)
#define arch_write_lock(l)		queued_write_lock(l)
#define arch_read_trylock(l)		queued_read_trylock(l)
#define arch_write_trylock(l)		queued_write_trylock(l)
#define arch_read_unlock(l)		queued_read_unlock(l)
#define arch_write_unlock(l)		queued_write_unlock(l)
#define arch_rwlock_is_contended(l)	queued_rwlock_is_contended(l)

#endif /* __ASM_GENERIC_QRWLOCK_H */
