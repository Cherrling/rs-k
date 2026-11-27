/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_FILE_LIFECYCLE_RUST_H
#define _LINUX_FILE_LIFECYCLE_RUST_H

#include <linux/types.h>

struct cred;
struct dentry;
struct file;
struct file_operations;
struct inode;
struct path;
struct vfsmount;

void fput_close_sync(struct file *file);
void fput_close(struct file *file);

#ifdef CONFIG_RUST_FILE_LIFECYCLE

struct file *alloc_empty_file(int flags, const struct cred *cred);
void __fput(struct file *file);

struct file *rust_helper_file_alloc_object(void);
void rust_helper_file_free_object(struct file *file);
int rust_helper_file_init_object(struct file *file, int flags,
				 const struct cred *cred);
const struct cred *rust_helper_file_current_cred(void);

long rust_helper_file_nr_files(void);
long rust_helper_file_nr_files_sum(void);
unsigned long rust_helper_file_max_files(void);
bool rust_helper_file_admin_capable(void);
void rust_helper_file_limit_warn(void);
void rust_helper_file_account_get(void);
void rust_helper_file_account_put(void);

bool rust_helper_file_is_err(const void *value);
void *rust_helper_file_err_ptr(long error);

int rust_helper_file_alloc_pseudo_path(const char *name,
				       struct inode *inode,
				       struct vfsmount *mnt,
				       struct path *path);
void rust_helper_file_abort_pseudo_path(struct path *path,
					struct inode *inode);
void rust_helper_file_init_path(struct file *file, const struct path *path,
				const struct file_operations *fops);
void rust_helper_file_get_path(struct file *file);
void rust_helper_file_set_mapping(struct file *file,
				  const struct file *base);
void rust_helper_file_set_fsnotify_nonotify(struct file *file);

struct dentry *rust_helper_file_dentry(const struct file *file);
struct vfsmount *rust_helper_file_mount(const struct file *file);
struct inode *rust_helper_file_inode(const struct file *file);
u32 rust_helper_file_mode(const struct file *file);
bool rust_helper_file_opened(const struct file *file);
bool rust_helper_file_early_release(const struct file *file);

void rust_helper_file_might_sleep(void);
void rust_helper_file_fsnotify_close(struct file *file);
void rust_helper_file_eventpoll_release(struct file *file);
void rust_helper_file_locks_remove(struct file *file);
void rust_helper_file_security_release(struct file *file);
bool rust_helper_file_needs_fasync(const struct file *file);
void rust_helper_file_call_fasync(struct file *file);
bool rust_helper_file_has_release(const struct file *file);
void rust_helper_file_call_release(struct file *file, struct inode *inode);
bool rust_helper_file_needs_cdev_put(const struct file *file,
				     const struct inode *inode, u32 mode);
void rust_helper_file_cdev_put(struct inode *inode);
void rust_helper_file_fops_put(struct file *file);
void rust_helper_file_owner_release(struct file *file);
void rust_helper_file_put_access(struct file *file);
void rust_helper_file_dput(struct dentry *dentry);
bool rust_helper_file_needs_unmount(u32 mode);
void rust_helper_file_dissolve_mount(struct vfsmount *mnt);
void rust_helper_file_mntput(struct vfsmount *mnt);

void rust_helper_file_security_free(struct file *file);
bool rust_helper_file_noaccount(const struct file *file);
void rust_helper_file_put_cred(struct file *file);
bool rust_helper_file_backing(const struct file *file);
void rust_helper_file_backing_free(struct file *file);

bool rust_helper_file_ref_put(struct file *file);
bool rust_helper_file_ref_put_close(struct file *file);
bool rust_helper_file_can_use_task_work(void);
bool rust_helper_file_queue_task_work(struct file *file);
void rust_helper_file_queue_delayed(struct file *file);

#ifdef CONFIG_FILE_LIFECYCLE_KUNIT_TEST
#define RUST_FILE_TEST_FAIL_LIMIT	1
#define RUST_FILE_TEST_FAIL_ALLOC	2
#define RUST_FILE_TEST_FAIL_INIT	3
#define RUST_FILE_TEST_FAIL_PSEUDO_PATH	4

u32 rust_file_test_alloc_count(void);
u32 rust_file_test_init_count(void);
u32 rust_file_test_path_count(void);
u32 rust_file_test_free_count(void);
u32 rust_file_test_release_count(void);
u32 rust_file_test_task_work_count(void);
u32 rust_file_test_delayed_count(void);
u32 rust_file_test_failure_count(void);
void rust_file_test_fail_next(u32 stage);
bool rust_file_test_queue_task_work(struct file *file);
#endif

#endif /* CONFIG_RUST_FILE_LIFECYCLE */

#ifdef CONFIG_FILE_LIFECYCLE_KUNIT_TEST
struct file *file_lifecycle_test_alloc_empty(int flags,
					     const struct cred *cred);
#endif

#endif /* _LINUX_FILE_LIFECYCLE_RUST_H */
