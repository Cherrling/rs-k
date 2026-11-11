// SPDX-License-Identifier: GPL-2.0
/*
 * Out-of-line refcount functions.
 */

#include <linux/mutex.h>
#include <linux/refcount.h>
#include <linux/spinlock.h>
#include <linux/bug.h>

#ifndef CONFIG_RUST_REFCOUNT

#define REFCOUNT_WARN(str)	WARN_ONCE(1, "refcount_t: " str ".\n")

void refcount_warn_saturate(refcount_t *r, enum refcount_saturation_type t)
{
	refcount_set(r, REFCOUNT_SATURATED);

	switch (t) {
	case REFCOUNT_ADD_NOT_ZERO_OVF:
		REFCOUNT_WARN("saturated; leaking memory");
		break;
	case REFCOUNT_ADD_OVF:
		REFCOUNT_WARN("saturated; leaking memory");
		break;
	case REFCOUNT_ADD_UAF:
		REFCOUNT_WARN("addition on 0; use-after-free");
		break;
	case REFCOUNT_SUB_UAF:
		REFCOUNT_WARN("underflow; use-after-free");
		break;
	case REFCOUNT_DEC_LEAK:
		REFCOUNT_WARN("decrement hit 0; leaking memory");
		break;
	default:
		REFCOUNT_WARN("unknown saturation event!?");
	}
}
EXPORT_SYMBOL(refcount_warn_saturate);

/**
 * refcount_dec_if_one - decrement a refcount if it is 1
 * @r: the refcount
 *
 * No atomic_t counterpart, it attempts a 1 -> 0 transition and returns the
 * success thereof.
 *
 * Like all decrement operations, it provides release memory order and provides
 * a control dependency.
 *
 * It can be used like a try-delete operator; this explicit case is provided
 * and not cmpxchg in generic, because that would allow implementing unsafe
 * operations.
 *
 * Return: true if the resulting refcount is 0, false otherwise
 */
bool refcount_dec_if_one(refcount_t *r)
{
	int val = 1;

	return atomic_try_cmpxchg_release(&r->refs, &val, 0);
}
EXPORT_SYMBOL(refcount_dec_if_one);

/**
 * refcount_dec_not_one - decrement a refcount if it is not 1
 * @r: the refcount
 *
 * No atomic_t counterpart, it decrements unless the value is 1, in which case
 * it will return false.
 *
 * Was often done like: atomic_add_unless(&var, -1, 1)
 *
 * Return: true if the decrement operation was successful, false otherwise
 */
bool refcount_dec_not_one(refcount_t *r)
{
	unsigned int new, val = atomic_read(&r->refs);

	do {
		if (unlikely(val == REFCOUNT_SATURATED))
			return true;

		if (val == 1)
			return false;

		new = val - 1;
		if (new > val) {
			WARN_ONCE(new > val, "refcount_t: underflow; use-after-free.\n");
			return true;
		}

	} while (!atomic_try_cmpxchg_release(&r->refs, &val, new));

	return true;
}
EXPORT_SYMBOL(refcount_dec_not_one);

/**
 * refcount_dec_and_mutex_lock - return holding mutex if able to decrement
 *                               refcount to 0
 * @r: the refcount
 * @lock: the mutex to be locked
 *
 * Similar to atomic_dec_and_mutex_lock(), it will WARN on underflow and fail
 * to decrement when saturated at REFCOUNT_SATURATED.
 *
 * Provides release memory ordering, such that prior loads and stores are done
 * before, and provides a control dependency such that free() must come after.
 * See the comment on top.
 *
 * Return: true and hold mutex if able to decrement refcount to 0, false
 *         otherwise
 */
bool refcount_dec_and_mutex_lock(refcount_t *r, struct mutex *lock)
{
	if (refcount_dec_not_one(r))
		return false;

	mutex_lock(lock);
	if (!refcount_dec_and_test(r)) {
		mutex_unlock(lock);
		return false;
	}

	return true;
}
EXPORT_SYMBOL(refcount_dec_and_mutex_lock);

/**
 * refcount_dec_and_lock - return holding spinlock if able to decrement
 *                         refcount to 0
 * @r: the refcount
 * @lock: the spinlock to be locked
 *
 * Similar to atomic_dec_and_lock(), it will WARN on underflow and fail to
 * decrement when saturated at REFCOUNT_SATURATED.
 *
 * Provides release memory ordering, such that prior loads and stores are done
 * before, and provides a control dependency such that free() must come after.
 * See the comment on top.
 *
 * Return: true and hold spinlock if able to decrement refcount to 0, false
 *         otherwise
 */
bool refcount_dec_and_lock(refcount_t *r, spinlock_t *lock)
{
	if (refcount_dec_not_one(r))
		return false;

	spin_lock(lock);
	if (!refcount_dec_and_test(r)) {
		spin_unlock(lock);
		return false;
	}

	return true;
}
EXPORT_SYMBOL(refcount_dec_and_lock);

/**
 * refcount_dec_and_lock_irqsave - return holding spinlock with disabled
 *                                 interrupts if able to decrement refcount to 0
 * @r: the refcount
 * @lock: the spinlock to be locked
 * @flags: saved IRQ-flags if the is acquired
 *
 * Same as refcount_dec_and_lock() above except that the spinlock is acquired
 * with disabled interrupts.
 *
 * Return: true and hold spinlock if able to decrement refcount to 0, false
 *         otherwise
 */
bool refcount_dec_and_lock_irqsave(refcount_t *r, spinlock_t *lock,
				   unsigned long *flags)
{
	if (refcount_dec_not_one(r))
		return false;

	spin_lock_irqsave(lock, *flags);
	if (!refcount_dec_and_test(r)) {
		spin_unlock_irqrestore(lock, *flags);
		return false;
	}

	return true;
}
EXPORT_SYMBOL(refcount_dec_and_lock_irqsave);

#else

#include <linux/refcount-rust.h>

int rust_refcount_atomic_read(const refcount_t *r)
{
	return atomic_read(&r->refs);
}

void rust_refcount_atomic_set(refcount_t *r, int value)
{
	atomic_set(&r->refs, value);
}

bool rust_refcount_try_cmpxchg_relaxed(refcount_t *r, int *old, int new)
{
	return atomic_try_cmpxchg_relaxed(&r->refs, old, new);
}

bool rust_refcount_try_cmpxchg_acquire(refcount_t *r, int *old, int new)
{
	return atomic_try_cmpxchg_acquire(&r->refs, old, new);
}

bool rust_refcount_try_cmpxchg_release(refcount_t *r, int *old, int new)
{
	return atomic_try_cmpxchg_release(&r->refs, old, new);
}

int rust_refcount_fetch_add_relaxed(int i, refcount_t *r)
{
	return atomic_fetch_add_relaxed(i, &r->refs);
}

int rust_refcount_fetch_sub_release(int i, refcount_t *r)
{
	return atomic_fetch_sub_release(i, &r->refs);
}

void rust_refcount_acquire_after_ctrl_dep(void)
{
	smp_acquire__after_ctrl_dep();
}

void rust_refcount_mutex_lock(struct mutex *lock)
{
	mutex_lock(lock);
}

void rust_refcount_mutex_unlock(struct mutex *lock)
{
	mutex_unlock(lock);
}

void rust_refcount_spin_lock(spinlock_t *lock)
{
	spin_lock(lock);
}

void rust_refcount_spin_unlock(spinlock_t *lock)
{
	spin_unlock(lock);
}

void rust_refcount_spin_lock_irqsave(spinlock_t *lock, unsigned long *flags)
{
	spin_lock_irqsave(lock, *flags);
}

void rust_refcount_spin_unlock_irqrestore(spinlock_t *lock,
					  unsigned long flags)
{
	spin_unlock_irqrestore(lock, flags);
}

#define REFCOUNT_WARN(str)	WARN_ONCE(1, "refcount_t: " str ".\n")

void rust_refcount_warn(int type)
{
	switch ((enum refcount_saturation_type)type) {
	case REFCOUNT_ADD_NOT_ZERO_OVF:
		REFCOUNT_WARN("saturated; leaking memory");
		break;
	case REFCOUNT_ADD_OVF:
		REFCOUNT_WARN("saturated; leaking memory");
		break;
	case REFCOUNT_ADD_UAF:
		REFCOUNT_WARN("addition on 0; use-after-free");
		break;
	case REFCOUNT_SUB_UAF:
		REFCOUNT_WARN("underflow; use-after-free");
		break;
	case REFCOUNT_DEC_LEAK:
		REFCOUNT_WARN("decrement hit 0; leaking memory");
		break;
	default:
		REFCOUNT_WARN("unknown saturation event!?");
	}
}

void rust_refcount_warn_dec_not_one(void)
{
	WARN_ONCE(true, "refcount_t: underflow; use-after-free.\n");
}

bool __refcount_add_not_zero(int i, refcount_t *r, int *oldp)
{
	return rust_refcount_add_not_zero(i, r, oldp);
}
EXPORT_SYMBOL(__refcount_add_not_zero);

bool __refcount_add_not_zero_limited_acquire(int i, refcount_t *r, int *oldp,
					     int limit)
{
	return rust_refcount_add_not_zero_limited_acquire(i, r, oldp, limit);
}
EXPORT_SYMBOL(__refcount_add_not_zero_limited_acquire);

void __refcount_add(int i, refcount_t *r, int *oldp)
{
	rust_refcount_add(i, r, oldp);
}
EXPORT_SYMBOL(__refcount_add);

bool __refcount_sub_and_test(int i, refcount_t *r, int *oldp)
{
	return rust_refcount_sub_and_test(i, r, oldp);
}
EXPORT_SYMBOL(__refcount_sub_and_test);

void __refcount_dec(refcount_t *r, int *oldp)
{
	rust_refcount_dec(r, oldp);
}
EXPORT_SYMBOL(__refcount_dec);

void refcount_warn_saturate(refcount_t *r, enum refcount_saturation_type t)
{
	rust_refcount_warn_saturate(r, t);
}
EXPORT_SYMBOL(refcount_warn_saturate);

bool refcount_dec_if_one(refcount_t *r)
{
	return rust_refcount_dec_if_one(r);
}
EXPORT_SYMBOL(refcount_dec_if_one);

bool refcount_dec_not_one(refcount_t *r)
{
	return rust_refcount_dec_not_one(r);
}
EXPORT_SYMBOL(refcount_dec_not_one);

bool refcount_dec_and_mutex_lock(refcount_t *r, struct mutex *lock)
{
	return rust_refcount_dec_and_mutex_lock(r, lock);
}
EXPORT_SYMBOL(refcount_dec_and_mutex_lock);

bool refcount_dec_and_lock(refcount_t *r, spinlock_t *lock)
{
	return rust_refcount_dec_and_lock(r, lock);
}
EXPORT_SYMBOL(refcount_dec_and_lock);

bool refcount_dec_and_lock_irqsave(refcount_t *r, spinlock_t *lock,
				   unsigned long *flags)
{
	return rust_refcount_dec_and_lock_irqsave(r, lock, flags);
}
EXPORT_SYMBOL(refcount_dec_and_lock_irqsave);

#endif /* CONFIG_RUST_REFCOUNT */
