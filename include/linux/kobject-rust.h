/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Internal helpers for the native Rust kobject lifecycle.
 */

#ifndef _LINUX_KOBJECT_RUST_H
#define _LINUX_KOBJECT_RUST_H

#include <linux/types.h>

struct kobject;
struct kobj_type;
struct kref;

#ifdef CONFIG_RUST_KOBJECT

void rust_kobject_init_internal(struct kobject *kobj);
void rust_kobject_init(struct kobject *kobj, const struct kobj_type *ktype);
struct kobject *rust_kobject_get(struct kobject *kobj);
struct kobject *rust_kobject_get_unless_zero(struct kobject *kobj);
void rust_kobject_put(struct kobject *kobj,
		      void (*release)(struct kref *kref));

bool rust_kobject_state_initialized(const struct kobject *kobj);
void rust_kobject_state_reset(struct kobject *kobj);
void rust_kobject_state_mark_initialized(struct kobject *kobj);
void rust_kobject_warn_invalid_pointer(void);
void rust_kobject_warn_missing_ktype(const struct kobject *kobj);
void rust_kobject_warn_reinitialized(const struct kobject *kobj);
void rust_kobject_warn_uninitialized(const struct kobject *kobj, bool is_put);

#endif /* CONFIG_RUST_KOBJECT */

#endif /* _LINUX_KOBJECT_RUST_H */
