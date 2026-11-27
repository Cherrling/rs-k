// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>
#include <linux/dcache.h>
#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/module.h>
#include <linux/namei.h>
#include <linux/open-entry-rust.h>
#include <linux/path.h>

static atomic_t open_test_create_count = ATOMIC_INIT(0);
static atomic_t open_test_atomic_count = ATOMIC_INIT(0);

static const struct file_operations open_test_file_operations;
static const struct inode_operations open_test_file_inode_operations;
static const struct inode_operations open_test_dir_inode_operations;
static const struct inode_operations open_test_atomic_inode_operations;

static struct inode *open_test_get_inode(struct super_block *sb, umode_t mode)
{
	struct inode *inode = new_inode(sb);

	if (!inode)
		return NULL;

	inode->i_ino = get_next_ino();
	inode->i_mode = mode;
	simple_inode_init_ts(inode);
	if (S_ISREG(mode)) {
		inode->i_op = &open_test_file_inode_operations;
		inode->i_fop = &open_test_file_operations;
	}
	return inode;
}

static int open_test_create_inode(struct mnt_idmap *idmap, struct inode *dir,
				  struct dentry *dentry, umode_t mode)
{
	struct inode *inode;

	inode = open_test_get_inode(dir->i_sb, mode | S_IFREG);
	if (!inode)
		return -ENOSPC;

	d_lookup_done(dentry);
	d_instantiate(dentry, inode);
	atomic_inc(&open_test_create_count);
	return 0;
}

static int open_test_atomic_create(struct inode *dir, struct dentry *dentry,
				   struct file *file, unsigned int open_flag,
				   umode_t mode)
{
	struct inode *inode;
	int error;

	if (!(open_flag & O_CREAT))
		return finish_no_open(file, NULL);

	inode = open_test_get_inode(dir->i_sb, mode | S_IFREG);
	if (!inode)
		return -ENOSPC;

	d_add(dentry, inode);
	atomic_inc(&open_test_atomic_count);
	file->f_mode |= FMODE_CREATED;
	error = finish_open(file, dentry, NULL);
	return error;
}

static int open_test_tmpfile_inode(struct mnt_idmap *idmap, struct inode *dir,
				   struct file *file, umode_t mode)
{
	struct inode *inode;

	inode = open_test_get_inode(dir->i_sb, mode | S_IFREG);
	if (!inode)
		return -ENOSPC;

	d_tmpfile(file, inode);
	return finish_open_simple(file, 0);
}

static const struct file_operations open_test_file_operations = {
};

static const struct inode_operations open_test_file_inode_operations = {
};

static const struct inode_operations open_test_dir_inode_operations = {
	.lookup		= simple_lookup,
	.create		= open_test_create_inode,
	.tmpfile	= open_test_tmpfile_inode,
};

static const struct inode_operations open_test_atomic_inode_operations = {
	.lookup		= simple_lookup,
	.atomic_open	= open_test_atomic_create,
	.tmpfile	= open_test_tmpfile_inode,
};

static int open_test_fill_super(struct super_block *sb, bool atomic)
{
	static const struct tree_descr entries[] = {
		{ "file", &open_test_file_operations, 0644 },
		{ "" },
	};
	int error;

	error = simple_fill_super(sb, 0x6f70656e, entries);
	if (error)
		return error;

	sb->s_root->d_inode->i_op =
		atomic ? &open_test_atomic_inode_operations :
			 &open_test_dir_inode_operations;
	return 0;
}

static int open_test_fill_create(struct super_block *sb,
				 struct fs_context *fc)
{
	return open_test_fill_super(sb, false);
}

static int open_test_fill_atomic(struct super_block *sb,
				 struct fs_context *fc)
{
	return open_test_fill_super(sb, true);
}

static int open_test_get_create_tree(struct fs_context *fc)
{
	return get_tree_nodev(fc, open_test_fill_create);
}

static int open_test_get_atomic_tree(struct fs_context *fc)
{
	return get_tree_nodev(fc, open_test_fill_atomic);
}

static const struct fs_context_operations open_test_create_context_ops = {
	.get_tree = open_test_get_create_tree,
};

static const struct fs_context_operations open_test_atomic_context_ops = {
	.get_tree = open_test_get_atomic_tree,
};

static int open_test_init_create_context(struct fs_context *fc)
{
	fc->ops = &open_test_create_context_ops;
	return 0;
}

static int open_test_init_atomic_context(struct fs_context *fc)
{
	fc->ops = &open_test_atomic_context_ops;
	return 0;
}

static struct file_system_type open_test_create_fs_type = {
	.name = "open_create_kunit",
	.init_fs_context = open_test_init_create_context,
	.kill_sb = kill_anon_super,
	.owner = THIS_MODULE,
};

static struct file_system_type open_test_atomic_fs_type = {
	.name = "open_atomic_kunit",
	.init_fs_context = open_test_init_atomic_context,
	.kill_sb = kill_anon_super,
	.owner = THIS_MODULE,
};

static struct vfsmount *open_test_mount(struct file_system_type *type)
{
	struct fs_context *fc;
	struct vfsmount *mnt;

	fc = fs_context_for_mount(type, SB_KERNMOUNT);
	if (IS_ERR(fc))
		return ERR_CAST(fc);

	mnt = fc_mount(fc);
	put_fs_context(fc);
	return mnt;
}

static struct vfsmount *open_test_mount_create(void)
{
	return open_test_mount(&open_test_create_fs_type);
}

static struct vfsmount *open_test_mount_atomic(void)
{
	return open_test_mount(&open_test_atomic_fs_type);
}

static struct file *open_test_open_file(struct vfsmount *root,
					const char *name, int flags,
					umode_t mode)
{
	struct path path = {
		.mnt = root,
		.dentry = root->mnt_root,
	};

	return file_open_root(&path, name, flags, mode);
}

static void open_test_ordinary(struct kunit *test)
{
	struct vfsmount *root = open_test_mount_create();
	struct file *file;

	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(root));

	file = open_test_open_file(root, "file", O_RDONLY, 0);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(file));
	KUNIT_EXPECT_TRUE(test, file->f_mode & FMODE_OPENED);
	KUNIT_EXPECT_PTR_EQ(test, root, file->f_path.mnt);
	fput(file);

	file = open_test_open_file(root, "file", O_PATH, 0);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(file));
	KUNIT_EXPECT_TRUE(test, file->f_mode & FMODE_PATH);
	KUNIT_EXPECT_TRUE(test, file->f_mode & FMODE_OPENED);
	fput(file);

	file = open_test_open_file(root, "missing", O_RDONLY, 0);
	KUNIT_EXPECT_TRUE(test, file == ERR_PTR(-ENOENT));

	mntput(root);
}

static void open_test_direct_open(struct kunit *test)
{
	struct vfsmount *root = open_test_mount_create();
	struct file *file;
	struct path path;
	int error;

	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(root));

	error = vfs_path_lookup(root->mnt_root, root, "file", 0, &path);
	KUNIT_ASSERT_EQ(test, 0, error);

	file = alloc_empty_file(O_RDONLY, current_cred());
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(file));
	error = vfs_open(&path, file);
	KUNIT_EXPECT_EQ(test, 0, error);
	KUNIT_EXPECT_TRUE(test, file->f_mode & FMODE_OPENED);
	fput(file);

	file = alloc_empty_file(O_RDONLY | O_DIRECT, current_cred());
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(file));
	error = vfs_open(&path, file);
	KUNIT_EXPECT_EQ(test, -EINVAL, error);
	fput(file);

	path_put(&path);
	mntput(root);
}

static void open_test_finish(struct kunit *test)
{
	struct vfsmount *root = open_test_mount_create();
	struct file *file;
	struct path path;
	int error;

	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(root));
	error = vfs_path_lookup(root->mnt_root, root, "file", 0, &path);
	KUNIT_ASSERT_EQ(test, 0, error);

	file = alloc_empty_file(O_RDONLY, current_cred());
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(file));
	file->__f_path.mnt = path.mnt;
	error = finish_open(file, path.dentry, NULL);
	KUNIT_EXPECT_EQ(test, 0, error);
	KUNIT_EXPECT_TRUE(test, file->f_mode & FMODE_OPENED);
	fput(file);

	file = alloc_empty_file(O_RDONLY, current_cred());
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(file));
	error = finish_no_open(file, ERR_PTR(-EACCES));
	KUNIT_EXPECT_EQ(test, -EACCES, error);
	error = finish_no_open(file, NULL);
	KUNIT_EXPECT_EQ(test, 0, error);
	fput(file);

	path_put(&path);
	mntput(root);
}

static void open_test_create(struct kunit *test)
{
	struct vfsmount *root = open_test_mount_create();
	struct file *created_file;
	struct file *file;

	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(root));
	atomic_set(&open_test_create_count, 0);

	created_file = open_test_open_file(root, "new",
					   O_CREAT | O_EXCL | O_WRONLY,
					   0600);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(created_file));
	KUNIT_EXPECT_TRUE(test, created_file->f_mode & FMODE_CREATED);
	KUNIT_EXPECT_EQ(test, 1, atomic_read(&open_test_create_count));

	file = open_test_open_file(root, "new",
				   O_CREAT | O_EXCL | O_WRONLY, 0600);
	KUNIT_EXPECT_TRUE(test, file == ERR_PTR(-EEXIST));

	file = open_test_open_file(root, "new", O_CREAT | O_WRONLY, 0600);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(file));
	KUNIT_EXPECT_FALSE(test, file->f_mode & FMODE_CREATED);
	fput(file);
	fput(created_file);

	mntput(root);
}

static void open_test_atomic(struct kunit *test)
{
	struct vfsmount *root = open_test_mount_atomic();
	struct file *file;

	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(root));
	atomic_set(&open_test_atomic_count, 0);

	file = open_test_open_file(root, "atomic",
				   O_CREAT | O_EXCL | O_WRONLY, 0600);
	KUNIT_ASSERT_FALSE_MSG(test, IS_ERR_OR_NULL(file), "file=%pe", file);
	KUNIT_EXPECT_TRUE(test, file->f_mode & FMODE_CREATED);
	KUNIT_EXPECT_EQ(test, 1, atomic_read(&open_test_atomic_count));
	fput(file);

	file = open_test_open_file(root, "atomic",
				   O_CREAT | O_EXCL | O_WRONLY, 0600);
	KUNIT_EXPECT_TRUE(test, file == ERR_PTR(-EEXIST));

	file = open_test_open_file(root, "missing", O_RDONLY, 0);
	KUNIT_EXPECT_TRUE(test, file == ERR_PTR(-ENOENT));

	mntput(root);
}

static void open_test_vfs_lookup_open(struct kunit *test)
{
	struct vfsmount *root = open_test_mount_create();
	struct path parent;
	struct qstr create = QSTR_INIT("lookup", 6);
	struct qstr existing = QSTR_INIT("file", 4);
	struct qstr missing = QSTR_INIT("absent", 6);
	struct file *file;

	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(root));
	parent.mnt = root;
	parent.dentry = root->mnt_root;
	atomic_set(&open_test_create_count, 0);

	file = vfs_lookup_open(&parent, &create, O_CREAT | O_EXCL | O_WRONLY,
			       0600);
	KUNIT_ASSERT_FALSE_MSG(test, IS_ERR_OR_NULL(file), "file=%pe", file);
	KUNIT_EXPECT_TRUE(test, file->f_mode & FMODE_CREATED);
	KUNIT_EXPECT_EQ(test, 1, atomic_read(&open_test_create_count));
	fput(file);

	file = vfs_lookup_open(&parent, &create, O_CREAT | O_EXCL | O_WRONLY,
			       0600);
	KUNIT_EXPECT_TRUE(test, file == ERR_PTR(-EEXIST));

	file = vfs_lookup_open(&parent, &existing, O_RDONLY, 0);
	KUNIT_ASSERT_FALSE_MSG(test, IS_ERR_OR_NULL(file), "file=%pe", file);
	KUNIT_EXPECT_TRUE(test, file->f_mode & FMODE_OPENED);
	fput(file);

	file = vfs_lookup_open(&parent, &missing, O_RDONLY, 0);
	KUNIT_EXPECT_TRUE(test, file == ERR_PTR(-ENOENT));

	mntput(root);
}

static void open_test_atomic_dentry_create(struct kunit *test)
{
	struct vfsmount *root = open_test_mount_atomic();
	struct dentry *child;
	struct file *file;
	struct path path;
	struct qstr last = QSTR_INIT("atomic-create", 13);

	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(root));
	atomic_set(&open_test_atomic_count, 0);
	child = start_creating(&nop_mnt_idmap, root->mnt_root, &last);
	KUNIT_ASSERT_FALSE(test, IS_ERR(child));

	path.mnt = root;
	path.dentry = child;
	file = dentry_create(&path, O_CREAT | O_EXCL | O_WRONLY, 0600,
			     current_cred());
	KUNIT_ASSERT_FALSE_MSG(test, IS_ERR_OR_NULL(file), "file=%pe", file);
	KUNIT_EXPECT_TRUE(test, file->f_mode & FMODE_CREATED);
	KUNIT_EXPECT_EQ(test, 1, atomic_read(&open_test_atomic_count));
	fput(file);

	end_creating(path.dentry);
	mntput(root);
}

static void open_test_tmpfile(struct kunit *test)
{
	struct vfsmount *root = open_test_mount_create();
	struct file *file;

	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(root));

	file = open_test_open_file(root, ".", O_TMPFILE | O_RDWR, 0600);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(file));
	KUNIT_EXPECT_TRUE(test, file->f_mode & FMODE_OPENED);
	fput(file);

	mntput(root);
}

static void open_test_dentry_create(struct kunit *test)
{
	struct vfsmount *root = open_test_mount_create();
	struct dentry *child;
	struct file *file;
	struct path path;
	struct qstr last = QSTR_INIT("dentry", 6);

	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(root));
	child = start_creating(&nop_mnt_idmap, root->mnt_root, &last);
	KUNIT_ASSERT_FALSE(test, IS_ERR(child));

	path.mnt = root;
	path.dentry = child;
	file = dentry_create(&path, O_CREAT | O_EXCL | O_WRONLY, 0600,
			     current_cred());
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(file));
	KUNIT_EXPECT_TRUE(test, file->f_mode & FMODE_OPENED);
	fput(file);

	end_creating(path.dentry);
	mntput(root);
}

#ifdef CONFIG_RUST_OPEN_ENTRY
static void open_test_retry(struct kunit *test)
{
	struct vfsmount *root = open_test_mount_create();
	struct file *file;

	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(root));

	rust_open_test_reset();
	rust_open_test_fail_next(RUST_OPEN_TEST_FAIL_ECHILD);
	file = open_test_open_file(root, "file", O_RDONLY, 0);
	KUNIT_ASSERT_FALSE_MSG(test, IS_ERR_OR_NULL(file), "file=%pe", file);
	KUNIT_EXPECT_EQ(test, 2, rust_open_test_attempts());
	KUNIT_EXPECT_EQ(test, LOOKUP_FOLLOW, rust_open_test_flags());
	fput(file);

	rust_open_test_reset();
	rust_open_test_fail_next(RUST_OPEN_TEST_FAIL_ECHILD);
	rust_open_test_fail_retry(RUST_OPEN_TEST_FAIL_EOPENSTALE);
	file = open_test_open_file(root, "file", O_RDONLY, 0);
	KUNIT_ASSERT_FALSE_MSG(test, IS_ERR_OR_NULL(file), "file=%pe", file);
	KUNIT_EXPECT_EQ(test, 3, rust_open_test_attempts());
	KUNIT_EXPECT_EQ(test, LOOKUP_REVAL | LOOKUP_FOLLOW,
			rust_open_test_flags());
	fput(file);

	rust_open_test_reset();
	mntput(root);
}
#endif

static struct kunit_case open_test_cases[] = {
	KUNIT_CASE(open_test_ordinary),
	KUNIT_CASE(open_test_direct_open),
	KUNIT_CASE(open_test_finish),
	KUNIT_CASE(open_test_create),
	KUNIT_CASE(open_test_atomic),
	KUNIT_CASE(open_test_vfs_lookup_open),
	KUNIT_CASE(open_test_atomic_dentry_create),
	KUNIT_CASE(open_test_tmpfile),
	KUNIT_CASE(open_test_dentry_create),
#ifdef CONFIG_RUST_OPEN_ENTRY
	KUNIT_CASE(open_test_retry),
#endif
	{}
};

static int open_test_suite_init(struct kunit_suite *suite)
{
	int error;

	error = register_filesystem(&open_test_create_fs_type);
	if (error)
		return error;

	error = register_filesystem(&open_test_atomic_fs_type);
	if (error)
		unregister_filesystem(&open_test_create_fs_type);
	return error;
}

static void open_test_suite_exit(struct kunit_suite *suite)
{
	unregister_filesystem(&open_test_atomic_fs_type);
	unregister_filesystem(&open_test_create_fs_type);
}

static struct kunit_suite open_test_suite = {
	.name = "open_entry",
	.suite_init = open_test_suite_init,
	.suite_exit = open_test_suite_exit,
	.test_cases = open_test_cases,
};

kunit_test_suite(open_test_suite);

MODULE_DESCRIPTION("KUnit tests for file open orchestration");
MODULE_LICENSE("GPL");
