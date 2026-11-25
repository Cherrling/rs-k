/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_CLASS_RUST_H
#define _LINUX_CLASS_RUST_H

struct class;
struct kobj_type;
struct kset;
struct subsys_private;

#ifdef CONFIG_RUST_CLASS

struct kset *rust_helper_class_kset(void);
const struct kobj_type *rust_helper_class_ktype(void);
int rust_helper_class_set_name(struct subsys_private *sp, const char *name);
void rust_helper_class_warn_missing_namespace(const struct class *cls);
void rust_helper_class_warn_missing_ns_type(const struct class *cls);
void rust_helper_class_create_release(const struct class *cls);

#endif /* CONFIG_RUST_CLASS */

#endif /* _LINUX_CLASS_RUST_H */
