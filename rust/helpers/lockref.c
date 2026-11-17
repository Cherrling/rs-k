// SPDX-License-Identifier: GPL-2.0

#include <linux/lockref-rust.h>

#ifdef CONFIG_RUST_LOCKREF

#if USE_CMPXCHG_LOCKREF

static struct lockref rust_lockref_from_raw(u64 raw)
{
	struct lockref value;

	value.lock_count = raw;
	return value;
}

u64 rust_lockref_read_raw(const struct lockref *lockref)
{
	return READ_ONCE(lockref->lock_count);
}

int rust_lockref_raw_count(u64 raw)
{
	return rust_lockref_from_raw(raw).count;
}

bool rust_lockref_raw_unlocked(u64 raw)
{
	struct lockref value = rust_lockref_from_raw(raw);

	return arch_spin_value_unlocked(value.lock.rlock.raw_lock);
}

bool rust_lockref_try_update(struct lockref *lockref, u64 *old, int count)
{
	struct lockref value = rust_lockref_from_raw(*old);

	value.count = count;
	return try_cmpxchg64_relaxed(&lockref->lock_count, old,
				     value.lock_count);
}

#else

u64 rust_lockref_read_raw(const struct lockref *lockref)
{
	return 0;
}

int rust_lockref_raw_count(u64 raw)
{
	return 0;
}

bool rust_lockref_raw_unlocked(u64 raw)
{
	return false;
}

bool rust_lockref_try_update(struct lockref *lockref, u64 *old, int count)
{
	return false;
}

#endif /* USE_CMPXCHG_LOCKREF */

void rust_lockref_lock(struct lockref *lockref)
{
	spin_lock(&lockref->lock);
}

void rust_lockref_unlock(struct lockref *lockref)
{
	spin_unlock(&lockref->lock);
}

int rust_lockref_locked_read(const struct lockref *lockref)
{
	return lockref->count;
}

void rust_lockref_locked_write(struct lockref *lockref, int count)
{
	lockref->count = count;
}

void rust_lockref_mark_dead_under_lock(struct lockref *lockref, int count)
{
	assert_spin_locked(&lockref->lock);
	lockref->count = count;
}

#endif /* CONFIG_RUST_LOCKREF */
