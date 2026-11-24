/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _LINUX_RWLOCK_RUST_H
#define _LINUX_RWLOCK_RUST_H

#include <linux/types.h>

struct qrwlock;

/*
 * Queued read-write lock count layout.
 *
 * The low writer mask contains either the 0xff writer-locked value or the
 * 0x100 waiting flag. Reader acquisitions add the reader bias above it.
 */
#define QRW_WAITING		0x100
#define QRW_LOCKED		0x0ff
#define QRW_WMASK		0x1ff
#define QRW_READER_SHIFT	9
#define QRW_BIAS		0x200

#ifdef CONFIG_RUST_RWLOCK

/*
 * Narrow C helpers used by the Rust state machine.
 */
int rust_qrwlock_read_count(struct qrwlock *lock);
int rust_qrwlock_add_return_acquire(struct qrwlock *lock, int value);
int rust_qrwlock_sub_return_release(struct qrwlock *lock, int value);
bool rust_qrwlock_try_cmpxchg_acquire(struct qrwlock *lock,
				      int *count, int new);
void rust_qrwlock_add(struct qrwlock *lock, int value);
void rust_qrwlock_sub(struct qrwlock *lock, int value);
void rust_qrwlock_or(struct qrwlock *lock, int value);
void rust_qrwlock_store_release_unlock(struct qrwlock *lock);
bool rust_qrwlock_in_interrupt(void);
void rust_qrwlock_wait_lock(struct qrwlock *lock);
void rust_qrwlock_wait_unlock(struct qrwlock *lock);
void rust_qrwlock_wait_writer_release_acquire(struct qrwlock *lock);
int rust_qrwlock_wait_waiting_relaxed(struct qrwlock *lock);
void rust_qrwlock_trace_read_begin(struct qrwlock *lock);
void rust_qrwlock_trace_write_begin(struct qrwlock *lock);
void rust_qrwlock_trace_end(struct qrwlock *lock);

#endif /* CONFIG_RUST_RWLOCK */

#endif /* _LINUX_RWLOCK_RUST_H */
