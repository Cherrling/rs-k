/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Internal helpers and C callback boundaries for the native Rust kobject
 * lifecycle.
 */

#ifndef _LINUX_KOBJECT_RUST_H
#define _LINUX_KOBJECT_RUST_H

#include <linux/types.h>

struct kobject;
struct kobj_type;
struct kref;
struct kset;
struct work_struct;

#if defined(CONFIG_RUST_KOBJECT) && defined(CONFIG_DEBUG_KOBJECT_RELEASE)
#include <linux/workqueue_types.h>
#endif

#ifdef CONFIG_RUST_KOBJECT

void rust_kobject_init_internal(struct kobject *kobj);

bool rust_kobject_state_initialized(const struct kobject *kobj);
bool rust_kobject_state_in_sysfs(const struct kobject *kobj);
bool rust_kobject_state_add_uevent_sent(const struct kobject *kobj);
bool rust_kobject_state_remove_uevent_sent(const struct kobject *kobj);
void rust_kobject_state_reset(struct kobject *kobj);
void rust_kobject_state_mark_initialized(struct kobject *kobj);
void rust_kobject_state_mark_in_sysfs(struct kobject *kobj, bool in_sysfs);
void rust_kobject_warn_invalid_pointer(void);
void rust_kobject_warn_missing_ktype(const struct kobject *kobj);
void rust_kobject_warn_reinitialized(const struct kobject *kobj);
void rust_kobject_warn_uninitialized(const struct kobject *kobj, bool is_put);

void rust_helper_kobject_remove_groups(struct kobject *kobj);
void rust_helper_kobject_remove_dir(struct kobject *kobj);
void rust_helper_kobject_kset_leave(struct kobject *kobj);

#ifdef CONFIG_DEBUG_KOBJECT_RELEASE
void rust_helper_kobject_schedule_delayed_release(struct kobject *kobj,
						  work_func_t func,
						  unsigned long delay);
#endif

#endif /* CONFIG_RUST_KOBJECT */

#ifdef CONFIG_RUST_KSET

void rust_helper_kset_init_objects(struct kset *kset);
int rust_helper_kset_add(struct kset *kset);
void rust_helper_kset_warn_missing_ktype(void);
void rust_helper_kset_free_name(struct kset *kset);
void rust_helper_kset_uevent_add(struct kset *kset);
void rust_helper_kset_lock(struct kset *kset);
void rust_helper_kset_unlock(struct kset *kset);
struct kset *rust_helper_kset_alloc(void);
void rust_helper_kset_free(struct kset *kset);
int rust_helper_kset_set_name(struct kset *kset, const char *name);
const struct kobj_type *rust_helper_kset_ktype(void);

#endif /* CONFIG_RUST_KSET */

#endif /* _LINUX_KOBJECT_RUST_H */
