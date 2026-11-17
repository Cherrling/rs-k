// SPDX-License-Identifier: GPL-2.0
#include <linux/dec-and-lock-rust.h>
#include <linux/export.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/atomic.h>

#ifdef CONFIG_RUST_DEC_AND_LOCK

int rust_dec_and_lock_atomic_add_unless(atomic_t *atomic)
{
	return atomic_add_unless(atomic, -1, 1);
}

bool rust_dec_and_lock_atomic_dec_and_test(atomic_t *atomic)
{
	return atomic_dec_and_test(atomic);
}

void rust_dec_and_lock_spin_lock(spinlock_t *lock)
{
	spin_lock(lock);
}

void rust_dec_and_lock_spin_unlock(spinlock_t *lock)
{
	spin_unlock(lock);
}

void rust_dec_and_lock_spin_lock_irqsave(spinlock_t *lock,
					 unsigned long *flags)
{
	spin_lock_irqsave(lock, *flags);
}

void rust_dec_and_lock_spin_unlock_irqrestore(spinlock_t *lock,
					      unsigned long flags)
{
	spin_unlock_irqrestore(lock, flags);
}

void rust_dec_and_lock_raw_lock(raw_spinlock_t *lock)
{
	raw_spin_lock(lock);
}

void rust_dec_and_lock_raw_unlock(raw_spinlock_t *lock)
{
	raw_spin_unlock(lock);
}

void rust_dec_and_lock_raw_lock_irqsave(raw_spinlock_t *lock,
					unsigned long *flags)
{
	raw_spin_lock_irqsave(lock, *flags);
}

void rust_dec_and_lock_raw_unlock_irqrestore(raw_spinlock_t *lock,
					     unsigned long flags)
{
	raw_spin_unlock_irqrestore(lock, flags);
}

void rust_dec_and_lock_mutex_lock(struct mutex *lock)
{
	mutex_lock(lock);
}

void rust_dec_and_lock_mutex_unlock(struct mutex *lock)
{
	mutex_unlock(lock);
}

#endif /* CONFIG_RUST_DEC_AND_LOCK */

/*
 * This is an implementation of the notion of "decrement a
 * reference count, and return locked if it decremented to zero".
 *
 * NOTE NOTE NOTE! This is _not_ equivalent to
 *
 *	if (atomic_dec_and_test(&atomic)) {
 *		spin_lock(&lock);
 *		return 1;
 *	}
 *	return 0;
 *
 * because the spin-lock and the decrement must be
 * "atomic".
 */
int atomic_dec_and_lock(atomic_t *atomic, spinlock_t *lock)
{
#ifdef CONFIG_RUST_DEC_AND_LOCK
	return rust_dec_and_lock(atomic, lock);
#else
	/* Subtract 1 from counter unless that drops it to 0 (ie. it was 1) */
	if (atomic_add_unless(atomic, -1, 1))
		return 0;

	/* Otherwise do it the slow way */
	spin_lock(lock);
	if (atomic_dec_and_test(atomic))
		return 1;
	spin_unlock(lock);
	return 0;
#endif /* CONFIG_RUST_DEC_AND_LOCK */
}

EXPORT_SYMBOL(atomic_dec_and_lock);

int _atomic_dec_and_lock_irqsave(atomic_t *atomic, spinlock_t *lock,
				 unsigned long *flags)
{
#ifdef CONFIG_RUST_DEC_AND_LOCK
	return rust_dec_and_lock_irqsave(atomic, lock, flags);
#else
	/* Subtract 1 from counter unless that drops it to 0 (ie. it was 1) */
	if (atomic_add_unless(atomic, -1, 1))
		return 0;

	/* Otherwise do it the slow way */
	spin_lock_irqsave(lock, *flags);
	if (atomic_dec_and_test(atomic))
		return 1;
	spin_unlock_irqrestore(lock, *flags);
	return 0;
#endif /* CONFIG_RUST_DEC_AND_LOCK */
}
EXPORT_SYMBOL(_atomic_dec_and_lock_irqsave);

int atomic_dec_and_raw_lock(atomic_t *atomic, raw_spinlock_t *lock)
{
#ifdef CONFIG_RUST_DEC_AND_LOCK
	return rust_dec_and_raw_lock(atomic, lock);
#else
	/* Subtract 1 from counter unless that drops it to 0 (ie. it was 1) */
	if (atomic_add_unless(atomic, -1, 1))
		return 0;

	/* Otherwise do it the slow way */
	raw_spin_lock(lock);
	if (atomic_dec_and_test(atomic))
		return 1;
	raw_spin_unlock(lock);
	return 0;
#endif /* CONFIG_RUST_DEC_AND_LOCK */
}
EXPORT_SYMBOL(atomic_dec_and_raw_lock);

int _atomic_dec_and_raw_lock_irqsave(atomic_t *atomic, raw_spinlock_t *lock,
				     unsigned long *flags)
{
#ifdef CONFIG_RUST_DEC_AND_LOCK
	return rust_dec_and_raw_lock_irqsave(atomic, lock, flags);
#else
	/* Subtract 1 from counter unless that drops it to 0 (ie. it was 1) */
	if (atomic_add_unless(atomic, -1, 1))
		return 0;

	/* Otherwise do it the slow way */
	raw_spin_lock_irqsave(lock, *flags);
	if (atomic_dec_and_test(atomic))
		return 1;
	raw_spin_unlock_irqrestore(lock, *flags);
	return 0;
#endif /* CONFIG_RUST_DEC_AND_LOCK */
}
EXPORT_SYMBOL(_atomic_dec_and_raw_lock_irqsave);
