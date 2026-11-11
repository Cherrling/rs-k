// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Queued read/write locks
 *
 * (C) Copyright 2013-2014 Hewlett-Packard Development Company, L.P.
 *
 * Authors: Waiman Long <waiman.long@hp.com>
 */
#include <linux/smp.h>
#include <linux/bug.h>
#include <linux/cpumask.h>
#include <linux/percpu.h>
#include <linux/hardirq.h>
#include <linux/spinlock.h>
#include <linux/rwlock-rust.h>
#include <trace/events/lock.h>

#ifdef CONFIG_RUST_RWLOCK
int rust_qrwlock_read_count(struct qrwlock *lock)
{
	return atomic_read(&lock->cnts);
}

int rust_qrwlock_add_return_acquire(struct qrwlock *lock, int value)
{
	return atomic_add_return_acquire(value, &lock->cnts);
}

int rust_qrwlock_sub_return_release(struct qrwlock *lock, int value)
{
	return atomic_sub_return_release(value, &lock->cnts);
}

bool rust_qrwlock_try_cmpxchg_acquire(struct qrwlock *lock,
				      int *count, int new)
{
	return atomic_try_cmpxchg_acquire(&lock->cnts, count, new);
}

void rust_qrwlock_add(struct qrwlock *lock, int value)
{
	atomic_add(value, &lock->cnts);
}

void rust_qrwlock_sub(struct qrwlock *lock, int value)
{
	atomic_sub(value, &lock->cnts);
}

void rust_qrwlock_or(struct qrwlock *lock, int value)
{
	atomic_or(value, &lock->cnts);
}

void rust_qrwlock_store_release_unlock(struct qrwlock *lock)
{
	/* Publish the writer's critical section before releasing the lock. */
	smp_store_release(&lock->wlocked, 0);
}

bool rust_qrwlock_in_interrupt(void)
{
	return in_interrupt();
}

void rust_qrwlock_wait_lock(struct qrwlock *lock)
{
	arch_spin_lock(&lock->wait_lock);
}

void rust_qrwlock_wait_unlock(struct qrwlock *lock)
{
	arch_spin_unlock(&lock->wait_lock);
}

void rust_qrwlock_wait_writer_release_acquire(struct qrwlock *lock)
{
	atomic_cond_read_acquire(&lock->cnts, !(VAL & QRW_LOCKED));
}

int rust_qrwlock_wait_waiting_relaxed(struct qrwlock *lock)
{
	return atomic_cond_read_relaxed(&lock->cnts, VAL == QRW_WAITING);
}

void rust_qrwlock_trace_read_begin(struct qrwlock *lock)
{
	trace_contention_begin(lock, LCB_F_SPIN | LCB_F_READ);
}

void rust_qrwlock_trace_write_begin(struct qrwlock *lock)
{
	trace_contention_begin(lock, LCB_F_SPIN | LCB_F_WRITE);
}

void rust_qrwlock_trace_end(struct qrwlock *lock)
{
	trace_contention_end(lock, 0);
}

/*
 * The queued read-write lock API remains inline in modules. These adapters
 * preserve the non-GPL export class of the existing lock interface.
 */
bool rust_qrwlock_read_lock(struct qrwlock *lock)
{
	return rust_qrwlock_read_fast(lock);
}
EXPORT_SYMBOL(rust_qrwlock_read_lock);

bool rust_qrwlock_read_trylock(struct qrwlock *lock)
{
	return rust_qrwlock_read_trylock_fast(lock);
}
EXPORT_SYMBOL(rust_qrwlock_read_trylock);

void rust_qrwlock_read_unlock(struct qrwlock *lock)
{
	rust_qrwlock_read_unlock_fast(lock);
}
EXPORT_SYMBOL(rust_qrwlock_read_unlock);

bool rust_qrwlock_write_lock(struct qrwlock *lock)
{
	return rust_qrwlock_write_fast(lock);
}
EXPORT_SYMBOL(rust_qrwlock_write_lock);

bool rust_qrwlock_write_trylock(struct qrwlock *lock)
{
	return rust_qrwlock_write_trylock_fast(lock);
}
EXPORT_SYMBOL(rust_qrwlock_write_trylock);

void rust_qrwlock_write_unlock(struct qrwlock *lock)
{
	rust_qrwlock_write_unlock_fast(lock);
}
EXPORT_SYMBOL(rust_qrwlock_write_unlock);
#endif /* CONFIG_RUST_RWLOCK */

/**
 * queued_read_lock_slowpath - acquire read lock of a queued rwlock
 * @lock: Pointer to queued rwlock structure
 */
void __lockfunc queued_read_lock_slowpath(struct qrwlock *lock)
{
#ifdef CONFIG_RUST_RWLOCK
	rust_qrwlock_read_lock_slowpath(lock);
#else
	/*
	 * Readers come here when they cannot get the lock without waiting
	 */
	if (unlikely(in_interrupt())) {
		/*
		 * Readers in interrupt context will get the lock immediately
		 * if the writer is just waiting (not holding the lock yet),
		 * so spin with ACQUIRE semantics until the lock is available
		 * without waiting in the queue.
		 */
		atomic_cond_read_acquire(&lock->cnts, !(VAL & QRW_LOCKED));
		return;
	}
	atomic_sub(QRW_BIAS, &lock->cnts);

	trace_contention_begin(lock, LCB_F_SPIN | LCB_F_READ);

	/*
	 * Put the reader into the wait queue
	 */
	arch_spin_lock(&lock->wait_lock);
	atomic_add(QRW_BIAS, &lock->cnts);

	/*
	 * The ACQUIRE semantics of the following spinning code ensure
	 * that accesses can't leak upwards out of our subsequent critical
	 * section in the case that the lock is currently held for write.
	 */
	atomic_cond_read_acquire(&lock->cnts, !(VAL & QRW_LOCKED));

	/*
	 * Signal the next one in queue to become queue head
	 */
	arch_spin_unlock(&lock->wait_lock);

	trace_contention_end(lock, 0);
#endif
}
EXPORT_SYMBOL(queued_read_lock_slowpath);

/**
 * queued_write_lock_slowpath - acquire write lock of a queued rwlock
 * @lock : Pointer to queued rwlock structure
 */
void __lockfunc queued_write_lock_slowpath(struct qrwlock *lock)
{
#ifdef CONFIG_RUST_RWLOCK
	rust_qrwlock_write_lock_slowpath(lock);
#else
	int cnts;

	trace_contention_begin(lock, LCB_F_SPIN | LCB_F_WRITE);

	/* Put the writer into the wait queue */
	arch_spin_lock(&lock->wait_lock);

	/* Try to acquire the lock directly if no reader is present */
	if (!(cnts = atomic_read(&lock->cnts)) &&
	    atomic_try_cmpxchg_acquire(&lock->cnts, &cnts, QRW_LOCKED))
		goto unlock;

	/* Set the waiting flag to notify readers that a writer is pending */
	atomic_or(QRW_WAITING, &lock->cnts);

	/* When no more readers or writers, set the locked flag */
	do {
		cnts = atomic_cond_read_relaxed(&lock->cnts, VAL == QRW_WAITING);
	} while (!atomic_try_cmpxchg_acquire(&lock->cnts, &cnts,
					     QRW_LOCKED));
unlock:
	arch_spin_unlock(&lock->wait_lock);

	trace_contention_end(lock, 0);
#endif
}
EXPORT_SYMBOL(queued_write_lock_slowpath);
