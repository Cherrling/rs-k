/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Internal helpers for the native Rust kref implementation.
 */

#ifndef _LINUX_KREF_RUST_H
#define _LINUX_KREF_RUST_H

#include <linux/spinlock_types.h>

struct kref;
struct mutex;

#ifdef CONFIG_RUST_KREF

void rust_kref_init(struct kref *kref);
unsigned int rust_kref_read(const struct kref *kref);
void rust_kref_get(struct kref *kref);
int rust_kref_put(struct kref *kref,
		  void (*release)(struct kref *kref));
int rust_kref_put_mutex(struct kref *kref,
			void (*release)(struct kref *kref),
			struct mutex *mutex); /* held during mutex release */
int rust_kref_put_lock(struct kref *kref,
		       void (*release)(struct kref *kref),
		       spinlock_t *lock); /* held during spinlock release */
int rust_kref_get_unless_zero(struct kref *kref);

#endif /* CONFIG_RUST_KREF */

#endif /* _LINUX_KREF_RUST_H */
