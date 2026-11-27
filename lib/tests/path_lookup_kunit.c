// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>
#include <linux/dcache.h>
#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/module.h>
#include <linux/namei.h>
#include <linux/path-lookup-rust.h>
#include <linux/path.h>

static int path_lookup_test_init_fs_context(struct fs_context *fc)
{
	static const struct fs_context_operations ops;

	fc->ops = &ops;
	return 0;
}

static struct file_system_type path_lookup_test_fs_type = {
	.name = "path_lookup_kunit",
	.init_fs_context = path_lookup_test_init_fs_context,
	.kill_sb = kill_anon_super,
	.owner = THIS_MODULE,
};

static int path_lookup_test_fill_super(struct super_block *sb,
				       struct fs_context *fc);

static int path_lookup_test_get_tree(struct fs_context *fc)
{
	return get_tree_nodev(fc, path_lookup_test_fill_super);
}

static int path_lookup_test_fill_super(struct super_block *sb,
				       struct fs_context *fc)
{
	static const struct tree_descr entries[] = {
		{ "file", NULL, 0644 },
		{ "" },
	};

	return simple_fill_super(sb, 0x706c6b75, entries);
}

static const struct fs_context_operations path_lookup_test_context_ops = {
	.get_tree = path_lookup_test_get_tree,
};

static struct vfsmount *path_lookup_test_mount(void)
{
	struct fs_context *fc;
	struct vfsmount *mnt;

	fc = fs_context_for_mount(&path_lookup_test_fs_type, SB_KERNMOUNT);
	if (IS_ERR(fc))
		return ERR_CAST(fc);
	fc->ops = &path_lookup_test_context_ops;

	mnt = fc_mount(fc);
	put_fs_context(fc);
	return mnt;
}

static void path_lookup_test_relative(struct kunit *test)
{
	struct vfsmount *root;
	struct path path;
	int error;

	root = path_lookup_test_mount();
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(root));

	error = vfs_path_lookup(root->mnt_root, root, "file", 0, &path);
	KUNIT_ASSERT_EQ(test, 0, error);
	KUNIT_EXPECT_PTR_EQ(test, root, path.mnt);
	KUNIT_EXPECT_STREQ(test, "file", path.dentry->d_name.name);
	path_put(&path);

	error = vfs_path_lookup(root->mnt_root, root, "missing", 0, &path);
	KUNIT_EXPECT_EQ(test, -ENOENT, error);

	error = vfs_path_lookup(root->mnt_root, root, "file",
				LOOKUP_DIRECTORY, &path);
	KUNIT_EXPECT_EQ(test, -ENOTDIR, error);

	mntput(root);
}

static void path_lookup_test_scoped_escape(struct kunit *test)
{
	struct vfsmount *root;
	struct path path;
	int error;

	root = path_lookup_test_mount();
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(root));

	error = vfs_path_lookup(root->mnt_root, root, "../file",
				LOOKUP_IN_ROOT, &path);
	KUNIT_ASSERT_EQ(test, 0, error);
	KUNIT_EXPECT_PTR_EQ(test, root, path.mnt);
	KUNIT_EXPECT_STREQ(test, "file", path.dentry->d_name.name);
	path_put(&path);

	error = vfs_path_lookup(root->mnt_root, root, "..",
				LOOKUP_BENEATH | LOOKUP_DIRECTORY,
				&path);
	KUNIT_EXPECT_EQ(test, -EXDEV, error);

	mntput(root);
}

static void path_lookup_test_parent(struct kunit *test)
{
	struct vfsmount *root;
	struct path root_path;
	struct path parent;
	struct filename *filename;
	struct qstr last;
	int error;

	root = path_lookup_test_mount();
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(root));
	root_path.mnt = root;
	root_path.dentry = root->mnt_root;

	filename = getname_kernel("file");
	KUNIT_ASSERT_FALSE(test, IS_ERR(filename));
	error = vfs_path_parent_lookup(filename, 0, &parent, &last, &root_path);
	KUNIT_EXPECT_EQ(test, 0, error);
	KUNIT_EXPECT_PTR_EQ(test, root->mnt_root, parent.dentry);
	KUNIT_EXPECT_PTR_EQ(test, root, parent.mnt);
	KUNIT_EXPECT_STREQ(test, "file", last.name);
	KUNIT_EXPECT_EQ(test, 4, last.len);
	path_put(&parent);
	putname(filename);

	filename = getname_kernel(".");
	KUNIT_ASSERT_FALSE(test, IS_ERR(filename));
	error = vfs_path_parent_lookup(filename, 0, &parent, &last, &root_path);
	KUNIT_EXPECT_EQ(test, -EINVAL, error);
	putname(filename);

	mntput(root);
}

static void path_lookup_test_mount_flags(struct kunit *test)
{
	struct vfsmount *root;
	struct path path;
	int error;

	root = path_lookup_test_mount();
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(root));

	error = vfs_path_lookup(root->mnt_root, root, ".",
				LOOKUP_DOWN | LOOKUP_DIRECTORY, &path);
	KUNIT_ASSERT_EQ(test, 0, error);
	KUNIT_EXPECT_PTR_EQ(test, root->mnt_root, path.dentry);
	path_put(&path);

	error = vfs_path_lookup(root->mnt_root, root, ".",
				LOOKUP_MOUNTPOINT | LOOKUP_DIRECTORY, &path);
	KUNIT_ASSERT_EQ(test, 0, error);
	KUNIT_EXPECT_PTR_EQ(test, root->mnt_root, path.dentry);
	path_put(&path);

	mntput(root);
}

static void path_lookup_test_kern_path(struct kunit *test)
{
	struct path path;
	int error;

	error = kern_path("/", LOOKUP_DIRECTORY, &path);
	KUNIT_ASSERT_EQ(test, 0, error);
	KUNIT_EXPECT_FALSE(test, IS_ERR_OR_NULL(path.mnt));
	KUNIT_EXPECT_FALSE(test, IS_ERR_OR_NULL(path.dentry));
	path_put(&path);
}

#ifdef CONFIG_RUST_PATH_LOOKUP
static void path_lookup_test_lookup_retry(struct kunit *test)
{
	struct vfsmount *root;
	struct path path;
	int error;

	root = path_lookup_test_mount();
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(root));

	rust_path_test_reset();
	rust_path_test_fail_lookup(RUST_PATH_TEST_FAIL_ECHILD);
	error = vfs_path_lookup(root->mnt_root, root, "file", 0, &path);
	KUNIT_EXPECT_EQ(test, 0, error);
	KUNIT_EXPECT_EQ(test, 2, rust_path_test_lookup_attempts());
	KUNIT_EXPECT_EQ(test, 0, rust_path_test_lookup_flags());
	path_put(&path);

	rust_path_test_reset();
	rust_path_test_fail_lookup(RUST_PATH_TEST_FAIL_ESTALE);
	error = vfs_path_lookup(root->mnt_root, root, "file", 0, &path);
	KUNIT_EXPECT_EQ(test, 0, error);
	KUNIT_EXPECT_EQ(test, 2, rust_path_test_lookup_attempts());
	KUNIT_EXPECT_EQ(test, LOOKUP_REVAL, rust_path_test_lookup_flags());
	path_put(&path);

	rust_path_test_reset();
	mntput(root);
}

static void path_lookup_test_parent_retry(struct kunit *test)
{
	struct vfsmount *root;
	struct path root_path;
	struct path parent;
	struct filename *filename;
	struct qstr last;
	int error;

	root = path_lookup_test_mount();
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(root));
	root_path.mnt = root;
	root_path.dentry = root->mnt_root;

	filename = getname_kernel("file");
	KUNIT_ASSERT_FALSE(test, IS_ERR(filename));

	rust_path_test_reset();
	rust_path_test_fail_parent(RUST_PATH_TEST_FAIL_ECHILD);
	error = vfs_path_parent_lookup(filename, 0, &parent, &last, &root_path);
	KUNIT_EXPECT_EQ(test, 0, error);
	KUNIT_EXPECT_EQ(test, 2, rust_path_test_parent_attempts());
	KUNIT_EXPECT_EQ(test, 0, rust_path_test_parent_flags());
	path_put(&parent);

	rust_path_test_reset();
	rust_path_test_fail_parent(RUST_PATH_TEST_FAIL_ESTALE);
	error = vfs_path_parent_lookup(filename, 0, &parent, &last, &root_path);
	KUNIT_EXPECT_EQ(test, 0, error);
	KUNIT_EXPECT_EQ(test, 2, rust_path_test_parent_attempts());
	KUNIT_EXPECT_EQ(test, LOOKUP_REVAL, rust_path_test_parent_flags());
	path_put(&parent);

	rust_path_test_reset();
	putname(filename);
	mntput(root);
}
#endif

static struct kunit_case path_lookup_test_cases[] = {
	KUNIT_CASE(path_lookup_test_relative),
	KUNIT_CASE(path_lookup_test_scoped_escape),
	KUNIT_CASE(path_lookup_test_parent),
	KUNIT_CASE(path_lookup_test_mount_flags),
	KUNIT_CASE(path_lookup_test_kern_path),
#ifdef CONFIG_RUST_PATH_LOOKUP
	KUNIT_CASE(path_lookup_test_lookup_retry),
	KUNIT_CASE(path_lookup_test_parent_retry),
#endif
	{}
};

static int path_lookup_test_suite_init(struct kunit_suite *suite)
{
	return register_filesystem(&path_lookup_test_fs_type);
}

static void path_lookup_test_suite_exit(struct kunit_suite *suite)
{
	unregister_filesystem(&path_lookup_test_fs_type);
}

static struct kunit_suite path_lookup_test_suite = {
	.name = "path_lookup",
	.suite_init = path_lookup_test_suite_init,
	.suite_exit = path_lookup_test_suite_exit,
	.test_cases = path_lookup_test_cases,
};

kunit_test_suite(path_lookup_test_suite);

MODULE_DESCRIPTION("KUnit tests for path lookup");
MODULE_LICENSE("GPL");
