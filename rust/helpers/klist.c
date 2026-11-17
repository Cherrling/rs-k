// SPDX-License-Identifier: GPL-2.0

#include <linux/klist.h>
#include <linux/spinlock.h>

__rust_helper void rust_helper_klist_list_init(struct list_head *list)
{
	INIT_LIST_HEAD(list);
}

__rust_helper void rust_helper_klist_list_add(struct list_head *new,
					      struct list_head *head)
{
	list_add(new, head);
}

__rust_helper void rust_helper_klist_list_add_tail(struct list_head *new,
						   struct list_head *head)
{
	list_add_tail(new, head);
}

__rust_helper void rust_helper_klist_list_del(struct list_head *entry)
{
	list_del(entry);
}

__rust_helper void rust_helper_klist_refcount_set(struct kref *kref, int value)
{
	refcount_set(&kref->refcount, value);
}

__rust_helper void rust_helper_klist_refcount_inc(struct kref *kref)
{
	kref_get(kref);
}

__rust_helper bool
rust_helper_klist_refcount_inc_not_zero(struct kref *kref)
{
	return kref_get_unless_zero(kref);
}

__rust_helper bool
rust_helper_klist_refcount_dec_and_test(struct kref *kref)
{
	return refcount_dec_and_test(&kref->refcount);
}

__rust_helper void rust_helper_klist_spin_lock(spinlock_t *lock)
{
	spin_lock(lock);
}

__rust_helper void rust_helper_klist_spin_unlock(spinlock_t *lock)
{
	spin_unlock(lock);
}

__rust_helper void
rust_helper_klist_spin_lock_irqsave(spinlock_t *lock, unsigned long *flags)
{
	spin_lock_irqsave(lock, *flags);
}

__rust_helper void
rust_helper_klist_spin_unlock_irqrestore(spinlock_t *lock,
					 unsigned long flags)
{
	spin_unlock_irqrestore(lock, flags);
}
