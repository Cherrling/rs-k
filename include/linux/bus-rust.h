/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_BUS_RUST_H
#define _LINUX_BUS_RUST_H

struct bus_type;
struct klist_node;
struct kobj_type;
struct kset;
struct subsys_private;

#ifdef CONFIG_RUST_BUS

struct kset *rust_helper_bus_kset(void);
const struct kobj_type *rust_helper_bus_ktype(void);
int rust_helper_bus_set_name(struct subsys_private *sp, const char *name);
void rust_helper_bus_init_notifier(struct subsys_private *sp);
void rust_helper_bus_klist_devices_get(struct klist_node *n);
void rust_helper_bus_klist_devices_put(struct klist_node *n);
int rust_helper_bus_create_uevent_file(const struct bus_type *bus);
void rust_helper_bus_remove_uevent_file(const struct bus_type *bus);
int rust_helper_bus_add_probe_files(const struct bus_type *bus);
void rust_helper_bus_remove_probe_files(const struct bus_type *bus);
void rust_helper_bus_debug_registered(const struct bus_type *bus);
void rust_helper_bus_debug_unregistering(const struct bus_type *bus);

#endif /* CONFIG_RUST_BUS */

#endif /* _LINUX_BUS_RUST_H */
