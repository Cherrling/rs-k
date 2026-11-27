/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Internal helpers for the native Rust superblock lifecycle.
 */

#ifndef _LINUX_SUPERBLOCK_RUST_H
#define _LINUX_SUPERBLOCK_RUST_H

#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/rcupdate.h>
#include <linux/user_namespace.h>
#include <linux/workqueue.h>

void put_super(struct super_block *s);
void rust_kill_super_notify(struct super_block *s);

#ifdef CONFIG_RUST_SUPERBLOCK_LIFECYCLE

struct super_block *rust_helper_super_alloc_slab(void);
struct user_namespace *
rust_helper_super_get_user_ns(struct user_namespace *user_ns);
struct user_namespace *rust_helper_super_init_user_ns(void);
void rust_helper_super_set_user_ns(struct super_block *s,
				   struct user_namespace *user_ns);
struct user_namespace *rust_helper_super_user_ns(struct super_block *s);
void rust_helper_super_init_umount(struct super_block *s,
				   struct file_system_type *type);
void rust_helper_super_lock_alloc(struct super_block *s);
int rust_helper_super_security_alloc(struct super_block *s);
int rust_helper_super_init_writer(struct super_block *s,
				  struct file_system_type *type,
				  unsigned int index);
void rust_helper_super_set_bdi_flags(struct super_block *s,
				     unsigned long flags);
void rust_helper_super_set_nodev(struct super_block *s);
void rust_helper_super_init_layout(struct super_block *s);
void rust_helper_super_fserror_mount(struct super_block *s);
void rust_helper_super_set_refs(struct super_block *s);
void rust_helper_super_init_rename_quota(struct super_block *s,
					 struct file_system_type *type);
void rust_helper_super_set_limits(struct super_block *s);
bool rust_helper_super_alloc_shrinker(struct super_block *s,
				      struct file_system_type *type);
int rust_helper_super_init_dentry_lru(struct super_block *s);
int rust_helper_super_init_inode_lru(struct super_block *s);
bool rust_helper_super_alloc_dev(struct super_block *s);
void rust_helper_super_set_min_writeback_pages(struct super_block *s);

void rust_helper_super_init_destroy_work(struct super_block *s,
					 void (*callback)(struct work_struct *work));
void rust_helper_super_schedule_destroy_work(struct super_block *s);
void rust_helper_super_call_rcu(struct super_block *s,
				void (*callback)(struct rcu_head *head));
void rust_helper_super_free_fsnotify(struct super_block *s);
void rust_helper_super_free_security(struct super_block *s);
void rust_helper_super_put_user_ns(struct super_block *s);
void rust_helper_super_free_dev(struct super_block *s);
void rust_helper_super_free_subtype(struct super_block *s);
void rust_helper_super_free_writer(struct super_block *s, unsigned int index);
void rust_helper_super_free(struct super_block *s);
void rust_helper_super_unlock_excl(struct super_block *s);
void rust_helper_super_destroy_lrus(struct super_block *s);
void rust_helper_super_free_shrinker(struct super_block *s);

bool rust_helper_super_passive_dec_and_test(struct super_block *s);
void rust_helper_super_passive_inc(struct super_block *s);
struct file_system_type *rust_helper_super_type(struct super_block *s);
void rust_helper_super_lock_global(void);
void rust_helper_super_unlock_global(void);
void rust_helper_super_unlink_global(struct super_block *s);
void rust_helper_super_warn_teardown(struct super_block *s);
void rust_helper_super_put_filesystem(struct file_system_type *type);

void rust_helper_super_lockdep_assert_unlocked(struct super_block *s);
bool rust_helper_super_flags(struct super_block *s, unsigned long flags);
bool rust_helper_super_has_dev_claim(struct super_block *s);
void rust_helper_super_put_dev_claim(struct super_block *s);
void rust_helper_super_wake(struct super_block *s, unsigned long flag);
void rust_helper_super_warn_once(bool condition);

bool rust_helper_super_active_dec_and_test(struct super_block *s);
bool rust_helper_super_active_dec_not_last(struct super_block *s);
bool rust_helper_super_active_inc_not_zero(struct super_block *s);
bool rust_helper_super_lock_excl(struct super_block *s);
void rust_helper_super_lock_excl_unconditional(struct super_block *s);
void rust_helper_super_kill_sb(struct super_block *s);
void rust_helper_super_wait_dead(struct super_block *s);

bool rust_helper_super_fc_global(struct fs_context *fc);
struct user_namespace *rust_helper_super_fc_user_ns(struct fs_context *fc);
struct file_system_type *rust_helper_super_fc_type(struct fs_context *fc);
unsigned long rust_helper_super_fc_sb_flags(struct fs_context *fc);
unsigned long rust_helper_super_fc_iflags(struct fs_context *fc);
bool rust_helper_super_fc_exclusive(struct fs_context *fc);
int rust_helper_super_fs_type_flags(struct file_system_type *type);
void rust_helper_super_fc_error_userns(struct fs_context *fc);
void rust_helper_super_fc_warn_exclusive(struct fs_context *fc);
void rust_helper_super_fc_warn_userns(struct fs_context *fc);
struct super_block *rust_helper_super_first_sb(struct file_system_type *type);
struct super_block *rust_helper_super_next_sb(struct super_block *s);
bool rust_helper_super_dead(struct super_block *s);
int rust_helper_super_call_test(int (*test)(struct super_block *,
					    struct fs_context *),
	struct super_block *s, struct fs_context *fc);
int rust_helper_super_call_set(int (*set)(struct super_block *,
					  struct fs_context *),
	struct super_block *s, struct fs_context *fc);
void rust_helper_super_set_fs_info(struct super_block *s,
				   struct fs_context *fc);
void rust_helper_super_clear_fs_info(struct super_block *s);
void rust_helper_super_clear_fc_fs_info(struct fs_context *fc);
bool rust_helper_super_dev_registered(struct super_block *s);
void rust_helper_super_set_type(struct super_block *s,
				struct file_system_type *type);
void rust_helper_super_or_iflags(struct super_block *s, unsigned long flags);
void rust_helper_super_set_id(struct super_block *s);
void rust_helper_super_add_global(struct super_block *s);
void rust_helper_super_add_instances(struct super_block *s);
struct file_system_type *
rust_helper_super_get_filesystem(struct file_system_type *type);
void rust_helper_super_register_shrinker(struct super_block *s);
struct super_block *rust_helper_super_error_ptr(int error);

#ifdef CONFIG_SUPERBLOCK_LIFECYCLE_KUNIT_TEST
void rust_super_test_reset(void);
void rust_super_test_fail(unsigned int stage);
unsigned int rust_super_test_counter(unsigned int index);
#endif

#endif /* CONFIG_RUST_SUPERBLOCK_LIFECYCLE */

#endif /* _LINUX_SUPERBLOCK_RUST_H */
