/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_DRIVER_RUST_H
#define _LINUX_DRIVER_RUST_H

struct device_driver;
struct subsys_private;

#ifdef CONFIG_RUST_DRIVER

int rust_helper_driver_kobject_init_and_add(struct device_driver *drv);
int rust_helper_driver_add_uevent_file(struct device_driver *drv);
void rust_helper_driver_remove_uevent_file(struct device_driver *drv);
int rust_helper_driver_add_bind_files(struct device_driver *drv);
void rust_helper_driver_remove_bind_files(struct device_driver *drv);
int rust_helper_driver_module_add(struct device_driver *drv);
void rust_helper_driver_module_remove(struct device_driver *drv);
void rust_helper_driver_bus_put(struct subsys_private *sp);
void rust_helper_driver_warn_unexpected_unregister(void);

#endif /* CONFIG_RUST_DRIVER */

#endif /* _LINUX_DRIVER_RUST_H */
