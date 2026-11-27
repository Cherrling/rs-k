/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Shared open state and helpers for the native Rust implementation.
 */

#ifndef _LINUX_OPEN_ENTRY_RUST_H
#define _LINUX_OPEN_ENTRY_RUST_H

#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/types.h>

struct open_flags {
	int open_flag;
	umode_t mode;
	int acc_mode;
	int intent;
	int lookup_flags;
};

struct file *alloc_empty_file(int flags, const struct cred *cred);
int vfs_open(const struct path *path, struct file *file);
int vfs_tmpfile(struct mnt_idmap *idmap, const struct path *parentpath,
		struct file *file, umode_t mode);
int lookup_noperm_common(struct qstr *qname, struct dentry *base);
struct file *do_file_open(int dfd, struct filename *pathname,
			  const struct open_flags *op);
struct file *do_file_open_root(const struct path *root, const char *name,
			       const struct open_flags *op);

#ifdef CONFIG_RUST_OPEN_ENTRY
enum rust_open_entry_mode {
	RUST_OPEN_MODE_READ = FMODE_READ,
	RUST_OPEN_MODE_WRITE = FMODE_WRITE,
	RUST_OPEN_MODE_LSEEK = FMODE_LSEEK,
	RUST_OPEN_MODE_PREAD = FMODE_PREAD,
	RUST_OPEN_MODE_PWRITE = FMODE_PWRITE,
	RUST_OPEN_MODE_PATH = FMODE_PATH,
	RUST_OPEN_MODE_ATOMIC_POS = FMODE_ATOMIC_POS,
	RUST_OPEN_MODE_WRITER = FMODE_WRITER,
	RUST_OPEN_MODE_CAN_READ = FMODE_CAN_READ,
	RUST_OPEN_MODE_CAN_WRITE = FMODE_CAN_WRITE,
	RUST_OPEN_MODE_OPENED = FMODE_OPENED,
	RUST_OPEN_MODE_CREATED = FMODE_CREATED,
	RUST_OPEN_MODE_CAN_ODIRECT = FMODE_CAN_ODIRECT,
	RUST_OPEN_MODE_NONOTIFY = FMODE_NONOTIFY,
};

enum rust_open_entry_flags {
	RUST_OPEN_FLAG_CREAT = O_CREAT,
	RUST_OPEN_FLAG_EXCL = O_EXCL,
	RUST_OPEN_FLAG_TRUNC = O_TRUNC,
	RUST_OPEN_FLAG_PATH = O_PATH,
	RUST_OPEN_FLAG_DIRECTORY = O_DIRECTORY,
	RUST_OPEN_FLAG_WRONLY = O_WRONLY,
	RUST_OPEN_FLAG_RDWR = O_RDWR,
	RUST_OPEN_FLAG_DIRECT = O_DIRECT,
	RUST_OPEN_FLAG_NOCTTY = O_NOCTTY,
	RUST_OPEN_FLAG_TMPFILE = __O_TMPFILE,
	RUST_OPEN_FLAG_REGULAR = __O_REGULAR,
};

enum rust_open_entry_lookup {
	RUST_OPEN_LOOKUP_FOLLOW = LOOKUP_FOLLOW,
	RUST_OPEN_LOOKUP_DIRECTORY = LOOKUP_DIRECTORY,
	RUST_OPEN_LOOKUP_CREATE = LOOKUP_CREATE,
	RUST_OPEN_LOOKUP_EXCL = LOOKUP_EXCL,
	RUST_OPEN_LOOKUP_RCU = LOOKUP_RCU,
	RUST_OPEN_LOOKUP_REVAL = LOOKUP_REVAL,
	RUST_OPEN_LOOKUP_OPEN = LOOKUP_OPEN,
};

enum rust_open_entry_type {
	RUST_OPEN_TYPE_MASK = S_IFMT,
	RUST_OPEN_TYPE_DIR = S_IFDIR,
	RUST_OPEN_TYPE_REGULAR = S_IFREG,
	RUST_OPEN_TYPE_LINK = S_IFLNK,
	RUST_OPEN_TYPE_BLOCK = S_IFBLK,
	RUST_OPEN_TYPE_CHAR = S_IFCHR,
	RUST_OPEN_TYPE_FIFO = S_IFIFO,
	RUST_OPEN_TYPE_SOCKET = S_IFSOCK,
};

const struct file_operations *rust_helper_open_empty_fops(void);
errseq_t rust_helper_open_sample_wb_err(struct address_space *mapping);
errseq_t rust_helper_open_sample_sb_err(struct file *file);
void rust_helper_open_set_fsnotify_mode(struct file *file, unsigned int mode);
void rust_helper_open_readcount_inc(struct inode *inode);
int rust_helper_open_get_write_access(struct file *file);
bool rust_helper_open_special_file(const struct inode *inode);
const struct file_operations *rust_helper_open_get_fops(struct inode *inode);
void rust_helper_open_put_fops(const struct file_operations *fops);
void rust_helper_open_warn_missing_fops(void);
void rust_helper_open_put_file_access(struct file *file);
int rust_helper_open_break_lease(struct file *file);
int rust_helper_open_fsnotify_perm(struct file *file);
void rust_helper_open_fsnotify_open(struct file *file);
int rust_helper_open_iocb_flags(struct file *file);
bool rust_helper_open_is_opened(const struct file *file);

struct dentry *rust_helper_open_nd_dentry(struct nameidata *nd);
struct inode *rust_helper_open_nd_dentry_inode(struct nameidata *nd);
struct path *rust_helper_open_nd_path(struct nameidata *nd);
void rust_helper_open_nd_set_dentry(struct nameidata *nd,
				    struct dentry *dentry);
void rust_helper_open_nd_add_flags(struct nameidata *nd, unsigned int flags);
void rust_helper_open_nd_clear_lookup_flags(struct nameidata *nd);
bool rust_helper_open_nd_trailing_slashes(struct nameidata *nd);
bool rust_helper_open_nd_in_rcu(struct nameidata *nd);
bool rust_helper_open_nd_try_to_unlazy(struct nameidata *nd);
void rust_helper_open_nd_put_link_if_needed(struct nameidata *nd);
const char *rust_helper_open_nd_handle_dots(struct nameidata *nd,
					    unsigned int type);
const char *rust_helper_open_nd_step_into(struct nameidata *nd,
					  struct dentry *dentry);
void rust_helper_open_nd_replace_final_dentry(struct nameidata *nd,
					      struct dentry *dentry);
void rust_helper_open_nd_warn_rcu_without_create(void);
struct dentry *rust_helper_open_d_lookup(struct nameidata *nd);
struct dentry *rust_helper_open_d_alloc_parallel(struct nameidata *nd);
struct dentry *rust_helper_open_lookup_fast(struct nameidata *nd);
bool rust_helper_open_d_in_lookup(const struct dentry *dentry);
void rust_helper_open_d_lookup_done(struct dentry *dentry);
struct dentry *rust_helper_open_dget(struct dentry *dentry);
int rust_helper_open_d_revalidate(struct nameidata *nd,
				  struct dentry *dentry);
bool rust_helper_open_dentry_positive(const struct dentry *dentry);
bool rust_helper_open_dentry_negative(const struct dentry *dentry);
bool rust_helper_open_dentry_dir(const struct dentry *dentry);
bool rust_helper_open_dentry_regular(const struct dentry *dentry);
bool rust_helper_open_dentry_can_lookup(const struct dentry *dentry);
bool rust_helper_open_dentry_really_negative(const struct dentry *dentry);
struct inode *rust_helper_open_dentry_inode(const struct dentry *dentry);
struct inode *rust_helper_open_dentry_backing_inode(const struct dentry *dentry);
struct dentry *rust_helper_open_dentry_parent(const struct dentry *dentry);
umode_t rust_helper_open_dentry_mode(const struct dentry *dentry);
void rust_helper_open_nd_audit_parent(struct nameidata *nd);
void rust_helper_open_nd_audit_inode(struct nameidata *nd,
				     const struct dentry *dentry);
void rust_helper_open_audit_child_create(struct inode *dir,
					 struct dentry *dentry);
int rust_helper_open_nd_want_write(struct nameidata *nd);
void rust_helper_open_nd_drop_write(struct nameidata *nd);
void rust_helper_open_nd_inode_lock(struct nameidata *nd, bool exclusive);
void rust_helper_open_nd_inode_unlock(struct nameidata *nd, bool exclusive);
bool rust_helper_open_nd_dead_dir(struct nameidata *nd);
struct mnt_idmap *rust_helper_open_nd_idmap(struct nameidata *nd);
struct mnt_idmap *rust_helper_open_path_idmap(const struct path *path);
umode_t rust_helper_open_prepare_mode(struct mnt_idmap *idmap,
				      struct inode *dir, umode_t mode);
umode_t rust_helper_open_prepare_regular_mode(struct mnt_idmap *idmap,
					      struct inode *dir,
					      umode_t mode);
int rust_helper_open_may_o_create(struct mnt_idmap *idmap,
				  struct nameidata *nd,
				  struct dentry *dentry, umode_t mode);
int rust_helper_open_path_may_o_create(struct mnt_idmap *idmap,
				       const struct path *path,
				       struct dentry *dentry, umode_t mode);
unsigned int rust_helper_open_namei_flags(int flag);
void rust_helper_open_warn_atomic_unset(const struct dentry *dentry);
int rust_helper_open_try_break_deleg(struct inode *inode,
				     struct inode **delegated);
int rust_helper_open_break_deleg_wait(struct inode *inode);
struct dentry *rust_helper_open_fs_lookup(struct inode *dir,
					  struct dentry *dentry,
					  struct nameidata *nd);
int rust_helper_open_fs_create(struct mnt_idmap *idmap, struct inode *dir,
			       struct dentry *dentry, umode_t mode);
void rust_helper_open_fsnotify_create(struct inode *dir,
				      struct dentry *dentry);
int rust_helper_open_may_create_in_sticky(struct mnt_idmap *idmap,
					  struct nameidata *nd,
					  struct inode *inode);
int rust_helper_open_may_open(struct mnt_idmap *idmap,
			      struct nameidata *nd, int acc_mode, int flag);
int rust_helper_open_handle_truncate(struct mnt_idmap *idmap,
				     struct file *file);
bool rust_helper_open_root_rejects_symlink(const struct path *root, int intent);
void rust_helper_open_init_vfs_lookup(struct nameidata *nd,
				      const struct path *parent,
				      const struct qstr *last,
				      int open_flag);
void rust_helper_open_warn_vfs_lookup(int mode, int open_flag);
void rust_helper_open_warn_invalid_result(void);
int rust_helper_open_normalize_cleanup_error(int error);

#ifdef CONFIG_OPEN_ENTRY_KUNIT_TEST
#define RUST_OPEN_TEST_FAIL_ECHILD 1
#define RUST_OPEN_TEST_FAIL_EOPENSTALE 2

void rust_open_test_reset(void);
void rust_open_test_fail_next(unsigned int failure);
void rust_open_test_fail_retry(unsigned int failure);
unsigned int rust_open_test_attempts(void);
unsigned int rust_open_test_flags(void);
#endif
#endif /* CONFIG_RUST_OPEN_ENTRY */

#endif /* _LINUX_OPEN_ENTRY_RUST_H */
