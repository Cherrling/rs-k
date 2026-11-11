/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_RWSEM_RUST_H
#define _LINUX_RWSEM_RUST_H

#include <linux/bits.h>
#include <linux/rwsem.h>
#include <linux/types.h>

#ifndef CONFIG_PREEMPT_RT

struct task_struct;

/*
 * The least significant bits of owner indicate reader ownership and whether
 * optimistic spinning must stop. Writer owners use the task pointer directly.
 */
#define RWSEM_READER_OWNED	BIT(0)
#define RWSEM_NONSPINNABLE	BIT(1)
#define RWSEM_OWNER_FLAGS_MASK	(RWSEM_READER_OWNED | RWSEM_NONSPINNABLE)

/*
 * The count stores phase state in low bits and the reader count above them.
 */
#define RWSEM_FLAG_WAITERS	BIT(1)
#define RWSEM_FLAG_HANDOFF	BIT(2)
#define RWSEM_FLAG_READFAIL	BIT(BITS_PER_LONG - 1)

#define RWSEM_READER_SHIFT	8
#define RWSEM_READER_BIAS	BIT(RWSEM_READER_SHIFT)
#define RWSEM_READER_MASK	(~(RWSEM_READER_BIAS - 1))
#define RWSEM_WRITER_MASK	RWSEM_WRITER_LOCKED
#define RWSEM_LOCK_MASK		(RWSEM_WRITER_MASK | RWSEM_READER_MASK)
#define RWSEM_READ_FAILED_MASK	(RWSEM_WRITER_MASK | RWSEM_FLAG_WAITERS | \
				 RWSEM_FLAG_HANDOFF | RWSEM_FLAG_READFAIL)

/* Reader slow-path results shared by the Rust state machine and C adapter. */
#define RWSEM_RUST_STOLE_LOCK		BIT(0)
#define RWSEM_RUST_STEAL_WAKE_READERS	BIT(1)

/* Contended writer transition results. */
#define RWSEM_RUST_WRITE_FAILED		0U
#define RWSEM_RUST_WRITE_ACQUIRED	1U
#define RWSEM_RUST_WRITE_HANDOFF	2U

/* Wake classification used after adding a waiter or releasing a lock. */
#define RWSEM_RUST_WAKE_NONE		0U
#define RWSEM_RUST_WAKE_ANY		1U
#define RWSEM_RUST_WAKE_CLASS_READERS	2U

enum rwsem_waiter_type {
	RWSEM_WAITING_FOR_WRITE,
	RWSEM_WAITING_FOR_READ
};

struct rwsem_waiter {
	struct list_head list;
	struct task_struct *task;
	enum rwsem_waiter_type type;
	unsigned long timeout;
	bool handoff_set;
};

#ifdef CONFIG_RUST_RWSEM

/*
 * Production state-machine entry points implemented in Rust.
 */
bool rust_rwsem_read_fast(struct rw_semaphore *sem, long *count);
bool rust_rwsem_read_trylock(struct rw_semaphore *sem);
unsigned int rust_rwsem_reader_slow_steal(struct rw_semaphore *sem,
					  long count);
bool rust_rwsem_write_fast(struct rw_semaphore *sem);
bool rust_rwsem_write_unqueued(struct rw_semaphore *sem);
unsigned int rust_rwsem_write_slow(struct rw_semaphore *sem,
				   bool allow_handoff, bool waiter_is_first,
				   bool first_handoff_set, bool waiters_empty);
void rust_rwsem_set_writer_owner(struct rw_semaphore *sem);
unsigned int rust_rwsem_cond_wake(struct rw_semaphore *sem, long count);
struct task_struct *rust_rwsem_owner(struct rw_semaphore *sem);
bool rust_rwsem_is_reader_owned(struct rw_semaphore *sem);
bool rust_rwsem_up_read(struct rw_semaphore *sem);
bool rust_rwsem_up_write(struct rw_semaphore *sem);
bool rust_rwsem_downgrade_write(struct rw_semaphore *sem);

/*
 * Narrow C helpers used by the Rust state machine.
 */
long rust_rwsem_read_count(struct rw_semaphore *sem);
long rust_rwsem_add_return_acquire(struct rw_semaphore *sem, long value);
long rust_rwsem_add_return_release(struct rw_semaphore *sem, long value);
long rust_rwsem_fetch_add_release(struct rw_semaphore *sem, long value);
bool rust_rwsem_try_cmpxchg_count_acquire(struct rw_semaphore *sem,
					  long *count, long new);
unsigned long rust_rwsem_read_owner(struct rw_semaphore *sem);
void rust_rwsem_set_owner_value(struct rw_semaphore *sem,
				unsigned long owner);
unsigned long rust_rwsem_current(void);
void rust_rwsem_clear_reader_owned(struct rw_semaphore *sem);
void rust_rwsem_set_nonspinnable(struct rw_semaphore *sem);
void rust_rwsem_clear_nonspinnable(struct rw_semaphore *sem);
bool rust_rwsem_warn_negative_count(struct rw_semaphore *sem, long count);
void rust_rwsem_warn_release_count(struct rw_semaphore *sem, long count);
void rust_rwsem_trace_release(struct rw_semaphore *sem);

#endif /* CONFIG_RUST_RWSEM */

#endif /* !CONFIG_PREEMPT_RT */

#endif /* _LINUX_RWSEM_RUST_H */
