/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_DEVICE_RUST_H
#define _LINUX_DEVICE_RUST_H

struct device;
struct kobj_type;
struct kobject;
struct subsys_private;

#ifdef CONFIG_RUST_DEVICE

const struct kobj_type *rust_helper_device_ktype(void);
void rust_helper_device_init_locks(struct device *dev);
void rust_helper_device_init_pm(struct device *dev);
void rust_helper_device_set_node_no_numa(struct device *dev);
void rust_helper_device_init_coherent(struct device *dev);
void rust_helper_device_init_private(struct device *dev);
int rust_helper_device_set_init_name(struct device *dev);
const char *rust_helper_device_name(const struct device *dev);
int rust_helper_device_set_bus_name(struct device *dev);
void rust_helper_device_debug_add(const struct device *dev);
void rust_helper_device_debug_unregistering(const struct device *dev);
int rust_helper_device_get_parent(struct device *dev, struct device *parent,
				  struct kobject **parent_kobj);
void rust_helper_device_inherit_parent_node(struct device *dev,
					    struct device *parent);
int rust_helper_device_kobject_add(struct device *dev);
void rust_helper_device_lock(struct device *dev);
void rust_helper_device_unlock(struct device *dev);
void rust_helper_device_set_ready_to_probe(struct device *dev);
void rust_helper_device_platform_notify(struct device *dev);
void rust_helper_device_platform_notify_remove(struct device *dev);
int rust_helper_device_add_uevent_file(struct device *dev);
void rust_helper_device_remove_uevent_file(struct device *dev);
int rust_helper_device_add_class_symlinks(struct device *dev);
void rust_helper_device_remove_class_symlinks(struct device *dev);
int rust_helper_device_add_attrs(struct device *dev);
void rust_helper_device_remove_attrs(struct device *dev);
int rust_helper_device_dpm_sysfs_add(struct device *dev);
void rust_helper_device_dpm_sysfs_remove(struct device *dev);
void rust_helper_device_pm_add(struct device *dev);
void rust_helper_device_pm_remove(struct device *dev);
bool rust_helper_device_has_devt(const struct device *dev);
int rust_helper_device_add_dev_file(struct device *dev);
void rust_helper_device_remove_dev_file(struct device *dev);
int rust_helper_device_add_sys_dev_entry(struct device *dev);
void rust_helper_device_remove_sys_dev_entry(struct device *dev);
int rust_helper_device_devtmpfs_add(struct device *dev);
int rust_helper_device_devtmpfs_remove(struct device *dev);
void rust_helper_device_fw_devlink_add(struct device *dev);
void rust_helper_device_fw_devlink_maybe_unblock(struct device *dev);
void rust_helper_device_class_add(struct device *dev,
				  struct subsys_private *sp);
void rust_helper_device_class_remove(struct device *dev,
				     struct subsys_private *sp);
void rust_helper_device_class_put(struct subsys_private *sp);
void rust_helper_device_set_driver_null(struct device *dev);
void rust_helper_device_clear_fwnode(struct device *dev);
void rust_helper_device_links_purge(struct device *dev);
unsigned int rust_helper_device_memalloc_noio_save(void);
void rust_helper_device_memalloc_noio_restore(unsigned int flags);
void rust_helper_device_cleanup_glue_dir(struct device *dev,
					 struct kobject *glue_dir);

#endif /* CONFIG_RUST_DEVICE */

#endif /* _LINUX_DEVICE_RUST_H */
