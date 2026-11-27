/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_VFS_IO_RUST_H
#define _LINUX_VFS_IO_RUST_H

#include <linux/fs.h>
#include <linux/types.h>

enum rust_vfs_io_mode {
	RUST_VFS_IO_MODE_READ = FMODE_READ,
	RUST_VFS_IO_MODE_WRITE = FMODE_WRITE,
	RUST_VFS_IO_MODE_LSEEK = FMODE_LSEEK,
	RUST_VFS_IO_MODE_CAN_READ = FMODE_CAN_READ,
	RUST_VFS_IO_MODE_CAN_WRITE = FMODE_CAN_WRITE,
};

enum rust_vfs_io_access {
	RUST_VFS_IO_READ = READ,
	RUST_VFS_IO_WRITE = WRITE,
	RUST_VFS_IO_MAY_READ = MAY_READ,
	RUST_VFS_IO_MAY_WRITE = MAY_WRITE,
};

enum rust_vfs_io_limit {
	RUST_VFS_IO_MAX_RW_COUNT = MAX_RW_COUNT,
};

bool rust_helper_vfs_io_access_ok(const void __user *buf, size_t count);
bool rust_helper_vfs_io_unsigned_offsets(struct file *file);
int rust_helper_vfs_io_security_perm(struct file *file, int mask);
int rust_helper_vfs_io_fsnotify_area_perm(struct file *file, int mask,
					  const loff_t *ppos, size_t count);
void rust_helper_vfs_io_fsnotify_access(struct file *file);
void rust_helper_vfs_io_fsnotify_modify(struct file *file);
void rust_helper_vfs_io_start_write(struct file *file);
void rust_helper_vfs_io_end_write(struct file *file);
ssize_t rust_helper_vfs_io_new_sync_read(struct file *file,
					 char __user *buf, size_t count,
					 loff_t *pos);
ssize_t rust_helper_vfs_io_new_sync_write(struct file *file,
					  const char __user *buf,
					  size_t count, loff_t *pos);
void rust_helper_vfs_io_account_read(ssize_t result);
void rust_helper_vfs_io_account_write(ssize_t result);

#ifdef CONFIG_VFS_IO_KUNIT_TEST
void vfs_io_kunit_reset_write_freeze(void);
u32 vfs_io_kunit_start_write_count(void);
u32 vfs_io_kunit_end_write_count(void);
int vfs_io_kunit_write_freeze_depth(void);
#endif

#endif /* _LINUX_VFS_IO_RUST_H */
