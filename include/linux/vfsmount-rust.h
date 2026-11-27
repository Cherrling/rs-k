/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Internal helpers for the native Rust vfsmount lifecycle.
 */

#ifndef _LINUX_VFSMOUNT_RUST_H
#define _LINUX_VFSMOUNT_RUST_H

#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/types.h>

struct callback_head;
struct dentry;
struct fs_context;
struct list_head;
struct mount;
struct mnt_idmap;

#ifdef CONFIG_RUST_VFSMOUNT_LIFECYCLE

/*
 * Internal clone flags and mount state bits. Keep these values synchronized
 * with fs/pnode.h and fs/mount.h.
 */
#define RUST_MNT_CL_SLAVE		0x02
#define RUST_MNT_CL_MAKE_SHARED		0x08
#define RUST_MNT_CL_PRIVATE		0x10
#define RUST_MNT_T_SHARED		0x01
#define RUST_MNT_T_UNBINDABLE		0x02

struct mount *clone_mnt(struct mount *old, struct dentry *root, int flag);
void mntput_no_expire(struct mount *mnt);
void rust_mnt_cleanup_delayed(struct mount *mnt);

struct mount *rust_helper_mnt_alloc_object(void);
void rust_helper_mnt_free_object(struct mount *mnt);
int rust_helper_mnt_alloc_cover(struct mount *mnt);
void rust_helper_mnt_free_cover(struct mount *mnt);
int rust_helper_mnt_alloc_id(struct mount *mnt);
void rust_helper_mnt_free_id(struct mount *mnt);
const char *rust_helper_mnt_dup_devname(const char *name);
void rust_helper_mnt_set_devname(struct mount *mnt, const char *name);
const char *rust_helper_mnt_devname(const struct mount *mnt);
void rust_helper_mnt_free_devname(struct mount *mnt);
bool rust_helper_mnt_alloc_pcp(struct mount *mnt);
void rust_helper_mnt_free_pcp(struct mount *mnt);
void rust_helper_mnt_init_layout(struct mount *mnt);
void rust_helper_mnt_set_default_idmap(struct mount *mnt);

int rust_helper_mnt_flags(const struct mount *mnt);
void rust_helper_mnt_set_flags(struct mount *mnt, int flags);
void rust_helper_mnt_or_flags(struct mount *mnt, int flags);
int rust_helper_mnt_t_flags(const struct mount *mnt);
void rust_helper_mnt_set_t_flags(struct mount *mnt, int flags);
int rust_helper_mnt_group_id(const struct mount *mnt);
void rust_helper_mnt_set_group_id(struct mount *mnt, int group_id);
int rust_helper_mnt_alloc_group_id(struct mount *mnt);
struct mnt_idmap *rust_helper_mnt_idmap_get(struct mount *mnt);
void rust_helper_mnt_set_idmap(struct mount *mnt, struct mnt_idmap *idmap);
void rust_helper_mnt_idmap_put(struct mount *mnt);
void rust_helper_mnt_set_shared(struct mount *mnt);
bool rust_helper_mnt_peers(const struct mount *mnt,
			   const struct mount *old);
void rust_helper_mnt_add_peer(struct mount *mnt, struct mount *old);
bool rust_helper_mnt_is_slave(const struct mount *mnt);
struct mount *rust_helper_mnt_master(const struct mount *mnt);
void rust_helper_mnt_add_slave(struct mount *mnt, struct mount *old);
void rust_helper_mnt_add_slave_behind(struct mount *mnt, struct mount *old);
void rust_helper_mnt_set_master(struct mount *mnt, struct mount *master);

struct super_block *rust_helper_mnt_dentry_super(const struct dentry *dentry);
struct vfsmount *rust_helper_mnt_vfsmount(struct mount *mnt);
struct mount *rust_helper_mnt_from_vfsmount(struct vfsmount *mnt);
struct vfsmount *rust_helper_mnt_path_mount(const struct path *path);
struct dentry *rust_helper_mnt_path_dentry(const struct path *path);
struct mount *rust_helper_mnt_from_callback_head(struct callback_head *head);
void rust_helper_mnt_active_inc(struct super_block *sb);
void rust_helper_mnt_set_root(struct mount *mnt, struct dentry *root);
void rust_helper_mnt_set_super(struct mount *mnt, struct super_block *sb);
void rust_helper_mnt_set_parent_self(struct mount *mnt);
void rust_helper_mnt_lock_instance(void);
void rust_helper_mnt_unlock_instance(void);
void rust_helper_mnt_add_instance(struct mount *mnt,
				 struct super_block *sb);

struct dentry *rust_helper_mnt_fc_root(const struct fs_context *fc);
const char *rust_helper_mnt_fc_source(const struct fs_context *fc);
bool rust_helper_mnt_fc_kern_mount(const struct fs_context *fc);
bool rust_helper_mnt_is_err(const void *value);
void *rust_helper_mnt_err_ptr(long error);
long rust_helper_mnt_ptr_err(const void *value);

void rust_helper_mnt_inc_count(struct mount *mnt);
void rust_helper_mnt_dec_count(struct mount *mnt);
void rust_helper_mnt_dec_put_fast(struct mount *mnt);
int rust_helper_mnt_get_count(struct mount *mnt);
void rust_helper_mnt_lock_hash(void);
void rust_helper_mnt_unlock_hash(void);
void rust_helper_mnt_rcu_read_lock(void);
void rust_helper_mnt_rcu_read_unlock(void);
bool rust_helper_mnt_has_namespace(const struct mount *mnt);
bool rust_helper_mnt_expiry_mark(const struct mount *mnt);
void rust_helper_mnt_clear_expiry_mark(struct mount *mnt);
void rust_helper_mnt_make_shortterm(struct mount *mnt);
void rust_helper_mnt_del_instance(struct mount *mnt);
bool rust_helper_mnt_expire_empty(const struct mount *mnt);
void rust_helper_mnt_remove_expire(struct mount *mnt);
void rust_helper_mnt_warn_mounted_children(const struct mount *mnt);
void rust_helper_mnt_warn_negative_count(int count);
void rust_helper_mnt_init_list(struct list_head *list);
void rust_helper_mnt_drop_covers(struct mount *mnt,
				 struct list_head *shrink_list);
void rust_helper_mnt_shrink_dentries(struct list_head *list);
void rust_helper_mnt_warn_writers(struct mount *mnt);
bool rust_helper_mnt_pins_empty(const struct mount *mnt);
void rust_helper_mnt_kill_pins(struct mount *mnt);
void rust_helper_mnt_fsnotify_delete(struct mount *mnt);
void rust_helper_mnt_dput_root(struct mount *mnt);
void rust_helper_mnt_deactivate_super(struct mount *mnt);
void rust_helper_mnt_call_rcu(struct mount *mnt,
			      void (*callback)(struct callback_head *head));
bool rust_helper_mnt_can_use_task_work(void);
bool rust_helper_mnt_queue_task_work(
	struct mount *mnt, void (*callback)(struct callback_head *head));
void rust_helper_mnt_queue_delayed(struct mount *mnt);
void rust_helper_mnt_flush_delayed(void);
void rust_helper_mnt_synchronize_rcu(void);

#ifdef CONFIG_VFSMOUNT_LIFECYCLE_KUNIT_TEST
#define RUST_MNT_TEST_FAIL_ALLOC		1
#define RUST_MNT_TEST_FAIL_COVER		2
#define RUST_MNT_TEST_FAIL_ID			3
#define RUST_MNT_TEST_FAIL_DEVNAME		4
#define RUST_MNT_TEST_FAIL_PCP			5
#define RUST_MNT_TEST_FAIL_GROUP		6

void rust_mnt_test_reset(void);
void rust_mnt_test_fail_next(unsigned int stage);
void rust_mnt_test_force_task_work(void);
unsigned int rust_mnt_test_counter(unsigned int index);
int rust_mnt_test_count(struct mount *mnt);
struct mount *rust_mnt_test_alloc(const char *name);
struct mount *rust_mnt_test_clone(struct mount *old, struct dentry *root,
				  int flags);
void rust_mnt_test_flush_delayed(void);
void rust_mnt_test_synchronize_rcu(void);
#endif

#endif /* CONFIG_RUST_VFSMOUNT_LIFECYCLE */

#ifdef CONFIG_VFSMOUNT_LIFECYCLE_KUNIT_TEST
void rust_helper_mnt_test_release_group_id(struct mount *mnt);
void vfsmount_lifecycle_test_flush(void);
#endif

#endif /* _LINUX_VFSMOUNT_RUST_H */
