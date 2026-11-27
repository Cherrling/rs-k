// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>
#include <linux/anon_inodes.h>
#include <linux/file.h>
#include <linux/file-lifecycle-rust.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/uio.h>
#include <linux/vfs-io-rust.h>

static atomic_t vfs_io_test_read_calls = ATOMIC_INIT(0);
static atomic_t vfs_io_test_write_calls = ATOMIC_INIT(0);
static atomic_t vfs_io_test_read_iter_calls = ATOMIC_INIT(0);
static atomic_t vfs_io_test_write_iter_calls = ATOMIC_INIT(0);
static atomic_t vfs_io_test_llseek_calls = ATOMIC_INIT(0);
static atomic_t vfs_io_test_write_freeze_seen = ATOMIC_INIT(0);
static atomic64_t vfs_io_test_length = ATOMIC64_INIT(0);
static atomic64_t vfs_io_test_position = ATOMIC64_INIT(0);

static ssize_t vfs_io_test_read_cb(struct file *file, char __user *buf,
				   size_t count, loff_t *pos)
{
	atomic_inc(&vfs_io_test_read_calls);
	atomic64_set(&vfs_io_test_length, count);
	if (pos) {
		*pos += 11;
		atomic64_set(&vfs_io_test_position, *pos);
	}
	return min_t(size_t, count, 3);
}

static ssize_t vfs_io_test_write_cb(struct file *file, const char __user *buf,
				    size_t count, loff_t *pos)
{
	atomic_inc(&vfs_io_test_write_calls);
	if (vfs_io_kunit_write_freeze_depth() == 1)
		atomic_inc(&vfs_io_test_write_freeze_seen);
	atomic64_set(&vfs_io_test_length, count);
	if (pos) {
		*pos += 13;
		atomic64_set(&vfs_io_test_position, *pos);
	}
	return min_t(size_t, count, 4);
}

static ssize_t vfs_io_test_read_iter_cb(struct kiocb *iocb,
					struct iov_iter *iter)
{
	size_t count = iov_iter_count(iter);

	atomic_inc(&vfs_io_test_read_iter_calls);
	atomic64_set(&vfs_io_test_length, count);
	iocb->ki_pos += 9;
	atomic64_set(&vfs_io_test_position, iocb->ki_pos);
	return min_t(size_t, count, 7);
}

static ssize_t vfs_io_test_write_iter_cb(struct kiocb *iocb,
					 struct iov_iter *iter)
{
	size_t count = iov_iter_count(iter);

	atomic_inc(&vfs_io_test_write_iter_calls);
	if (vfs_io_kunit_write_freeze_depth() == 1)
		atomic_inc(&vfs_io_test_write_freeze_seen);
	atomic64_set(&vfs_io_test_length, count);
	iocb->ki_pos += 15;
	atomic64_set(&vfs_io_test_position, iocb->ki_pos);
	return min_t(size_t, count, 8);
}

static loff_t vfs_io_test_seek_callback(struct file *file, loff_t offset,
					int whence)
{
	atomic_inc(&vfs_io_test_llseek_calls);
	return offset * 2 + whence;
}

static const struct file_operations vfs_io_test_legacy_ops = {
	.read = vfs_io_test_read_cb,
	.write = vfs_io_test_write_cb,
	.llseek = vfs_io_test_seek_callback,
};

static const struct file_operations vfs_io_test_iter_ops = {
	.read_iter = vfs_io_test_read_iter_cb,
	.write_iter = vfs_io_test_write_iter_cb,
	.llseek = vfs_io_test_seek_callback,
};

static const struct file_operations vfs_io_test_empty_ops = {
};

static const struct file_operations vfs_io_test_unsigned_read_ops = {
	.read = vfs_io_test_read_cb,
	.fop_flags = FOP_UNSIGNED_OFFSET,
};

static void vfs_io_test_reset(void)
{
	atomic_set(&vfs_io_test_read_calls, 0);
	atomic_set(&vfs_io_test_write_calls, 0);
	atomic_set(&vfs_io_test_read_iter_calls, 0);
	atomic_set(&vfs_io_test_write_iter_calls, 0);
	atomic_set(&vfs_io_test_llseek_calls, 0);
	atomic_set(&vfs_io_test_write_freeze_seen, 0);
	atomic64_set(&vfs_io_test_length, 0);
	atomic64_set(&vfs_io_test_position, 0);
	vfs_io_kunit_reset_write_freeze();
}

static struct file *vfs_io_test_file(struct kunit *test,
				     const struct file_operations *fops,
				     fmode_t mode)
{
	const fmode_t mask = FMODE_READ | FMODE_WRITE | FMODE_LSEEK |
			     FMODE_CAN_READ | FMODE_CAN_WRITE;
	struct file *file;
	int flags;

	/*
	 * file_init_path() increments i_readcount for O_RDONLY but not for
	 * O_WRONLY or O_RDWR. Pick flags that keep put_file_access() balanced
	 * after the test overrides f_mode.
	 */
	if ((mode & (FMODE_READ | FMODE_WRITE)) ==
	    (FMODE_READ | FMODE_WRITE))
		flags = O_RDWR;
	else if (mode & FMODE_READ)
		flags = O_RDONLY;
	else
		flags = O_WRONLY;

	file = anon_inode_getfile("[vfs_io]", fops, NULL, flags);
	if (IS_ERR(file)) {
		KUNIT_FAIL(test, "failed to allocate anon file: %ld", PTR_ERR(file));
		return NULL;
	}

	file->f_mode = (file->f_mode & ~mask) | mode;
	return file;
}

static void vfs_io_test_close(struct file *file)
{
	fput_close_sync(file);
}

static void vfs_io_test_read_legacy(struct kunit *test)
{
	char __user *buf = (char __user *)PAGE_SIZE;
	struct file *file;
	loff_t pos = 10;

	vfs_io_test_reset();
	file = vfs_io_test_file(test, &vfs_io_test_legacy_ops,
				FMODE_READ | FMODE_CAN_READ);
	KUNIT_ASSERT_NOT_NULL(test, file);

	KUNIT_EXPECT_EQ(test, 3, vfs_read(file, buf, 64, &pos));
	KUNIT_EXPECT_EQ(test, 64, atomic64_read(&vfs_io_test_length));
	KUNIT_EXPECT_EQ(test, 21, pos);
	KUNIT_EXPECT_EQ(test, 1, atomic_read(&vfs_io_test_read_calls));
	vfs_io_test_close(file);
}

static void vfs_io_test_read_iter(struct kunit *test)
{
	char __user *buf = (char __user *)PAGE_SIZE;
	struct file *file;
	loff_t pos = 20;

	vfs_io_test_reset();
	file = vfs_io_test_file(test, &vfs_io_test_iter_ops,
				FMODE_READ | FMODE_CAN_READ);
	KUNIT_ASSERT_NOT_NULL(test, file);

	KUNIT_EXPECT_EQ(test, 7, vfs_read(file, buf, 64, &pos));
	KUNIT_EXPECT_EQ(test, 29, pos);
	KUNIT_EXPECT_EQ(test, 1, atomic_read(&vfs_io_test_read_iter_calls));
	vfs_io_test_close(file);
}

static void vfs_io_test_read_without_mode(struct kunit *test)
{
	char __user *buf = (char __user *)PAGE_SIZE;
	struct file *file;

	vfs_io_test_reset();
	file = vfs_io_test_file(test, &vfs_io_test_legacy_ops, FMODE_CAN_READ);
	KUNIT_ASSERT_NOT_NULL(test, file);

	KUNIT_EXPECT_EQ(test, -EBADF, vfs_read(file, buf, 1, NULL));
	KUNIT_EXPECT_EQ(test, 0, atomic_read(&vfs_io_test_read_calls));
	vfs_io_test_close(file);
}

static void vfs_io_test_read_without_capability(struct kunit *test)
{
	char __user *buf = (char __user *)PAGE_SIZE;
	struct file *file;

	vfs_io_test_reset();
	file = vfs_io_test_file(test, &vfs_io_test_legacy_ops, FMODE_READ);
	KUNIT_ASSERT_NOT_NULL(test, file);

	KUNIT_EXPECT_EQ(test, -EINVAL, vfs_read(file, buf, 1, NULL));
	KUNIT_EXPECT_EQ(test, 0, atomic_read(&vfs_io_test_read_calls));
	vfs_io_test_close(file);
}

static void vfs_io_test_read_without_callback(struct kunit *test)
{
	char __user *buf = (char __user *)PAGE_SIZE;
	struct file *file;

	vfs_io_test_reset();
	file = vfs_io_test_file(test, &vfs_io_test_empty_ops,
				FMODE_READ | FMODE_CAN_READ);
	KUNIT_ASSERT_NOT_NULL(test, file);

	KUNIT_EXPECT_EQ(test, -EINVAL, vfs_read(file, buf, 1, NULL));
	vfs_io_test_close(file);
}

static void vfs_io_test_read_bad_user_range(struct kunit *test)
{
	char __user *bad = (char __user *)ULONG_MAX;
	struct file *file;

	vfs_io_test_reset();
	file = vfs_io_test_file(test, &vfs_io_test_legacy_ops,
				FMODE_READ | FMODE_CAN_READ);
	KUNIT_ASSERT_NOT_NULL(test, file);

	KUNIT_EXPECT_EQ(test, -EFAULT, vfs_read(file, bad, 1, NULL));
	KUNIT_EXPECT_EQ(test, 0, atomic_read(&vfs_io_test_read_calls));
	vfs_io_test_close(file);
}

static void vfs_io_test_read_max_count(struct kunit *test)
{
	char __user *buf = (char __user *)PAGE_SIZE;
	struct file *file;

	vfs_io_test_reset();
	file = vfs_io_test_file(test, &vfs_io_test_legacy_ops,
				FMODE_READ | FMODE_CAN_READ);
	KUNIT_ASSERT_NOT_NULL(test, file);

	KUNIT_EXPECT_EQ(test, 3,
			vfs_read(file, buf, MAX_RW_COUNT + PAGE_SIZE, NULL));
	KUNIT_EXPECT_EQ(test, MAX_RW_COUNT, atomic64_read(&vfs_io_test_length));
	vfs_io_test_close(file);
}

static void vfs_io_test_write_legacy(struct kunit *test)
{
	const char __user *buf = (const char __user *)PAGE_SIZE;
	struct file *file;
	loff_t pos = 30;

	vfs_io_test_reset();
	file = vfs_io_test_file(test, &vfs_io_test_legacy_ops,
				FMODE_WRITE | FMODE_CAN_WRITE);
	KUNIT_ASSERT_NOT_NULL(test, file);

	KUNIT_EXPECT_EQ(test, 4, vfs_write(file, buf, 64, &pos));
	KUNIT_EXPECT_EQ(test, 64, atomic64_read(&vfs_io_test_length));
	KUNIT_EXPECT_EQ(test, 43, pos);
	KUNIT_EXPECT_EQ(test, 1, atomic_read(&vfs_io_test_write_calls));
	KUNIT_EXPECT_EQ(test, 1,
			atomic_read(&vfs_io_test_write_freeze_seen));
	KUNIT_EXPECT_EQ(test, 1, vfs_io_kunit_start_write_count());
	KUNIT_EXPECT_EQ(test, 1, vfs_io_kunit_end_write_count());
	KUNIT_EXPECT_EQ(test, 0, vfs_io_kunit_write_freeze_depth());
	vfs_io_test_close(file);
}

static void vfs_io_test_write_iter(struct kunit *test)
{
	const char __user *buf = (const char __user *)PAGE_SIZE;
	struct file *file;
	loff_t pos = 50;

	vfs_io_test_reset();
	file = vfs_io_test_file(test, &vfs_io_test_iter_ops,
				FMODE_WRITE | FMODE_CAN_WRITE);
	KUNIT_ASSERT_NOT_NULL(test, file);

	KUNIT_EXPECT_EQ(test, 8, vfs_write(file, buf, 64, &pos));
	KUNIT_EXPECT_EQ(test, 65, pos);
	KUNIT_EXPECT_EQ(test, 1, atomic_read(&vfs_io_test_write_iter_calls));
	KUNIT_EXPECT_EQ(test, 1,
			atomic_read(&vfs_io_test_write_freeze_seen));
	KUNIT_EXPECT_EQ(test, 1, vfs_io_kunit_start_write_count());
	KUNIT_EXPECT_EQ(test, 1, vfs_io_kunit_end_write_count());
	KUNIT_EXPECT_EQ(test, 0, vfs_io_kunit_write_freeze_depth());
	vfs_io_test_close(file);
}

static void vfs_io_test_write_without_mode(struct kunit *test)
{
	const char __user *buf = (const char __user *)PAGE_SIZE;
	struct file *file;

	vfs_io_test_reset();
	file = vfs_io_test_file(test, &vfs_io_test_legacy_ops, FMODE_CAN_WRITE);
	KUNIT_ASSERT_NOT_NULL(test, file);

	KUNIT_EXPECT_EQ(test, -EBADF, vfs_write(file, buf, 1, NULL));
	KUNIT_EXPECT_EQ(test, 0, atomic_read(&vfs_io_test_write_calls));
	KUNIT_EXPECT_EQ(test, 0, vfs_io_kunit_start_write_count());
	vfs_io_test_close(file);
}

static void vfs_io_test_write_without_capability(struct kunit *test)
{
	const char __user *buf = (const char __user *)PAGE_SIZE;
	struct file *file;

	vfs_io_test_reset();
	file = vfs_io_test_file(test, &vfs_io_test_legacy_ops, FMODE_WRITE);
	KUNIT_ASSERT_NOT_NULL(test, file);

	KUNIT_EXPECT_EQ(test, -EINVAL, vfs_write(file, buf, 1, NULL));
	KUNIT_EXPECT_EQ(test, 0, atomic_read(&vfs_io_test_write_calls));
	KUNIT_EXPECT_EQ(test, 0, vfs_io_kunit_start_write_count());
	vfs_io_test_close(file);
}

static void vfs_io_test_write_without_callback(struct kunit *test)
{
	const char __user *buf = (const char __user *)PAGE_SIZE;
	struct file *file;

	vfs_io_test_reset();
	file = vfs_io_test_file(test, &vfs_io_test_empty_ops,
				FMODE_WRITE | FMODE_CAN_WRITE);
	KUNIT_ASSERT_NOT_NULL(test, file);

	KUNIT_EXPECT_EQ(test, -EINVAL, vfs_write(file, buf, 1, NULL));
	KUNIT_EXPECT_EQ(test, 1, vfs_io_kunit_start_write_count());
	KUNIT_EXPECT_EQ(test, 1, vfs_io_kunit_end_write_count());
	KUNIT_EXPECT_EQ(test, 0, vfs_io_kunit_write_freeze_depth());
	vfs_io_test_close(file);
}

static void vfs_io_test_write_bad_user_range(struct kunit *test)
{
	const char __user *bad = (const char __user *)ULONG_MAX;
	struct file *file;

	vfs_io_test_reset();
	file = vfs_io_test_file(test, &vfs_io_test_legacy_ops,
				FMODE_WRITE | FMODE_CAN_WRITE);
	KUNIT_ASSERT_NOT_NULL(test, file);

	KUNIT_EXPECT_EQ(test, -EFAULT, vfs_write(file, bad, 1, NULL));
	KUNIT_EXPECT_EQ(test, 0, atomic_read(&vfs_io_test_write_calls));
	KUNIT_EXPECT_EQ(test, 0, vfs_io_kunit_start_write_count());
	vfs_io_test_close(file);
}

static void vfs_io_test_write_max_count(struct kunit *test)
{
	const char __user *buf = (const char __user *)PAGE_SIZE;
	struct file *file;

	vfs_io_test_reset();
	file = vfs_io_test_file(test, &vfs_io_test_legacy_ops,
				FMODE_WRITE | FMODE_CAN_WRITE);
	KUNIT_ASSERT_NOT_NULL(test, file);

	KUNIT_EXPECT_EQ(test, 4,
			vfs_write(file, buf, MAX_RW_COUNT + PAGE_SIZE, NULL));
	KUNIT_EXPECT_EQ(test, MAX_RW_COUNT, atomic64_read(&vfs_io_test_length));
	vfs_io_test_close(file);
}

static void vfs_io_test_verify_count_overflow(struct kunit *test)
{
	struct file *file;

	file = vfs_io_test_file(test, &vfs_io_test_legacy_ops,
				FMODE_READ | FMODE_CAN_READ);
	KUNIT_ASSERT_NOT_NULL(test, file);

	KUNIT_EXPECT_EQ(test, -EINVAL,
			rw_verify_area(READ, file, NULL, (size_t)-1));
	vfs_io_test_close(file);
}

static void vfs_io_test_verify_negative_offset(struct kunit *test)
{
	struct file *file;
	loff_t pos = -1;

	file = vfs_io_test_file(test, &vfs_io_test_legacy_ops,
				FMODE_READ | FMODE_CAN_READ);
	KUNIT_ASSERT_NOT_NULL(test, file);

	KUNIT_EXPECT_EQ(test, -EINVAL, rw_verify_area(READ, file, &pos, 1));
	vfs_io_test_close(file);
}

static void vfs_io_test_verify_signed_limit(struct kunit *test)
{
	struct file *file;
	loff_t pos = LLONG_MAX;

	file = vfs_io_test_file(test, &vfs_io_test_legacy_ops,
				FMODE_READ | FMODE_CAN_READ);
	KUNIT_ASSERT_NOT_NULL(test, file);

	KUNIT_EXPECT_EQ(test, -EINVAL, rw_verify_area(READ, file, &pos, 1));
	vfs_io_test_close(file);
}

static void vfs_io_test_verify_unsigned_offset(struct kunit *test)
{
	struct file *file;
	loff_t pos = -1;

	file = vfs_io_test_file(test, &vfs_io_test_unsigned_read_ops,
				FMODE_READ | FMODE_CAN_READ);
	KUNIT_ASSERT_NOT_NULL(test, file);

	KUNIT_EXPECT_EQ(test, -EOVERFLOW, rw_verify_area(READ, file, &pos, 1));
	KUNIT_EXPECT_EQ(test, 0, rw_verify_area(READ, file, &pos, 0));
	pos = LLONG_MAX;
	KUNIT_EXPECT_EQ(test, 0, rw_verify_area(READ, file, &pos, 1));
	vfs_io_test_close(file);
}

static void vfs_io_test_verify_valid_range(struct kunit *test)
{
	struct file *file;
	loff_t pos = PAGE_SIZE;

	file = vfs_io_test_file(test, &vfs_io_test_legacy_ops,
				FMODE_READ | FMODE_CAN_READ);
	KUNIT_ASSERT_NOT_NULL(test, file);

	KUNIT_EXPECT_EQ(test, 0, rw_verify_area(READ, file, &pos, PAGE_SIZE));
	KUNIT_EXPECT_EQ(test, 0, rw_verify_area(READ, file, NULL, PAGE_SIZE));
	vfs_io_test_close(file);
}

static void vfs_io_test_llseek(struct kunit *test)
{
	struct file *file;

	vfs_io_test_reset();
	file = vfs_io_test_file(test, &vfs_io_test_legacy_ops,
				FMODE_LSEEK);
	KUNIT_ASSERT_NOT_NULL(test, file);
	KUNIT_EXPECT_EQ(test, 42, vfs_llseek(file, 21, SEEK_SET));
	KUNIT_EXPECT_EQ(test, 1, atomic_read(&vfs_io_test_llseek_calls));
	vfs_io_test_close(file);

	file = vfs_io_test_file(test, &vfs_io_test_legacy_ops, 0);
	KUNIT_ASSERT_NOT_NULL(test, file);
	KUNIT_EXPECT_EQ(test, -ESPIPE, vfs_llseek(file, 21, SEEK_SET));
	vfs_io_test_close(file);
}

static struct kunit_case vfs_io_test_cases[] = {
	KUNIT_CASE(vfs_io_test_read_legacy),
	KUNIT_CASE(vfs_io_test_read_iter),
	KUNIT_CASE(vfs_io_test_read_without_mode),
	KUNIT_CASE(vfs_io_test_read_without_capability),
	KUNIT_CASE(vfs_io_test_read_without_callback),
	KUNIT_CASE(vfs_io_test_read_bad_user_range),
	KUNIT_CASE(vfs_io_test_read_max_count),
	KUNIT_CASE(vfs_io_test_write_legacy),
	KUNIT_CASE(vfs_io_test_write_iter),
	KUNIT_CASE(vfs_io_test_write_without_mode),
	KUNIT_CASE(vfs_io_test_write_without_capability),
	KUNIT_CASE(vfs_io_test_write_without_callback),
	KUNIT_CASE(vfs_io_test_write_bad_user_range),
	KUNIT_CASE(vfs_io_test_write_max_count),
	KUNIT_CASE(vfs_io_test_verify_count_overflow),
	KUNIT_CASE(vfs_io_test_verify_negative_offset),
	KUNIT_CASE(vfs_io_test_verify_signed_limit),
	KUNIT_CASE(vfs_io_test_verify_unsigned_offset),
	KUNIT_CASE(vfs_io_test_verify_valid_range),
	KUNIT_CASE(vfs_io_test_llseek),
	{}
};

static struct kunit_suite vfs_io_test_suite = {
	.name = "vfs_io",
	.test_cases = vfs_io_test_cases,
};

kunit_test_suite(vfs_io_test_suite);

MODULE_DESCRIPTION("KUnit tests for VFS I/O orchestration");
MODULE_LICENSE("GPL");
