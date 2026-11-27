// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>
#include <linux/completion.h>
#include <linux/dcache.h>
#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/namei.h>
#include <linux/path.h>
#include <linux/rcupdate.h>
#include <linux/sched/task.h>
#include <linux/task_work.h>
#include <linux/vfsmount-rust.h>
#include <linux/workqueue.h>

#define VFSMOUNT_TEST_TIMEOUT		(10 * HZ)
#define VFSMOUNT_TEST_WORKERS		4

#define VFSMOUNT_COUNTER_ALLOC		0
#define VFSMOUNT_COUNTER_ALLOC_FAILURE	1
#define VFSMOUNT_COUNTER_CLEANUP	2
#define VFSMOUNT_COUNTER_FREE		3
#define VFSMOUNT_COUNTER_TASK_WORK	4
#define VFSMOUNT_COUNTER_DELAYED	5
#define VFSMOUNT_COUNTER_CLONE		6

struct vfsmount_worker_context {
	struct completion done;
	struct vfsmount *mnt;
};

static int vfsmount_test_init_fs_context(struct fs_context *fc)
{
	static const struct fs_context_operations ops;

	fc->ops = &ops;
	return 0;
}

static struct file_system_type vfsmount_test_fs_type = {
	.name = "vfsmount_lifecycle_test",
	.init_fs_context = vfsmount_test_init_fs_context,
	.kill_sb = kill_anon_super,
	.owner = THIS_MODULE,
};

static int vfsmount_test_fill_super(struct super_block *sb,
				    struct fs_context *fc);

static int vfsmount_test_get_tree(struct fs_context *fc)
{
	return get_tree_nodev(fc, vfsmount_test_fill_super);
}

static int vfsmount_test_fill_super(struct super_block *sb,
				    struct fs_context *fc)
{
	static const struct tree_descr empty_root[] = {
		{ "" },
	};

	return simple_fill_super(sb, 0x7666736d, empty_root);
}

static const struct fs_context_operations vfsmount_test_context_ops = {
	.get_tree = vfsmount_test_get_tree,
};

static struct vfsmount *vfsmount_test_create(int sb_flags)
{
	struct fs_context *fc;
	struct vfsmount *mnt;

	fc = fs_context_for_mount(&vfsmount_test_fs_type, sb_flags);
	if (IS_ERR(fc))
		return ERR_CAST(fc);
	fc->ops = &vfsmount_test_context_ops;

	mnt = fc_mount(fc);
	put_fs_context(fc);
	return mnt;
}

static bool vfsmount_test_wait(struct completion *done)
{
	return wait_for_completion_timeout(done, VFSMOUNT_TEST_TIMEOUT);
}

#ifdef CONFIG_RUST_VFSMOUNT_LIFECYCLE
static unsigned int vfsmount_test_counter(unsigned int index)
{
	return rust_mnt_test_counter(index);
}
#endif

static void vfsmount_lifecycle_test_null_operations(struct kunit *test)
{
	mntput(NULL);
	KUNIT_EXPECT_NULL(test, mntget(NULL));
	mnt_make_shortterm(NULL);
}

static void vfsmount_lifecycle_test_create_final(struct kunit *test)
{
	struct vfsmount *mnt;

#ifdef CONFIG_RUST_VFSMOUNT_LIFECYCLE
	rust_mnt_test_reset();
#endif
	mnt = vfsmount_test_create(0);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(mnt));
	KUNIT_EXPECT_PTR_EQ(test, mnt->mnt_root->d_sb, mnt->mnt_sb);

#ifdef CONFIG_RUST_VFSMOUNT_LIFECYCLE
	rust_mnt_test_force_task_work();
#endif
	mntput(mnt);
	task_work_run();
	vfsmount_lifecycle_test_flush();
	rcu_barrier();

#ifdef CONFIG_RUST_VFSMOUNT_LIFECYCLE
	KUNIT_EXPECT_EQ(test, 1, vfsmount_test_counter(VFSMOUNT_COUNTER_ALLOC));
	KUNIT_EXPECT_EQ(test, 1,
			vfsmount_test_counter(VFSMOUNT_COUNTER_CLEANUP));
	KUNIT_EXPECT_EQ(test, 1, vfsmount_test_counter(VFSMOUNT_COUNTER_FREE));
	KUNIT_EXPECT_EQ(test, 1,
			vfsmount_test_counter(VFSMOUNT_COUNTER_TASK_WORK));
#endif
}

static void vfsmount_lifecycle_test_shared_references(struct kunit *test)
{
	struct vfsmount *mnt;

#ifdef CONFIG_RUST_VFSMOUNT_LIFECYCLE
	rust_mnt_test_reset();
#endif
	mnt = vfsmount_test_create(SB_KERNMOUNT);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(mnt));

	mntget(mnt);
#ifdef CONFIG_RUST_VFSMOUNT_LIFECYCLE
	KUNIT_EXPECT_EQ(test, 2,
			rust_mnt_test_count(
				rust_helper_mnt_from_vfsmount(mnt)));
#endif
	mntput(mnt);
#ifdef CONFIG_RUST_VFSMOUNT_LIFECYCLE
	KUNIT_EXPECT_EQ(test, 1,
			rust_mnt_test_count(
				rust_helper_mnt_from_vfsmount(mnt)));
	KUNIT_EXPECT_EQ(test, 0,
			vfsmount_test_counter(VFSMOUNT_COUNTER_CLEANUP));
#endif

	mntput(mnt);
	rcu_barrier();

#ifdef CONFIG_RUST_VFSMOUNT_LIFECYCLE
	KUNIT_EXPECT_EQ(test, 1,
			vfsmount_test_counter(VFSMOUNT_COUNTER_CLEANUP));
	KUNIT_EXPECT_EQ(test, 1, vfsmount_test_counter(VFSMOUNT_COUNTER_FREE));
	KUNIT_EXPECT_EQ(test, 0,
			vfsmount_test_counter(VFSMOUNT_COUNTER_TASK_WORK));
#endif
}

static void vfsmount_lifecycle_test_clone_internal(struct kunit *test)
{
	struct vfsmount *base;
	struct vfsmount *clone;
	struct path path;

#ifdef CONFIG_RUST_VFSMOUNT_LIFECYCLE
	rust_mnt_test_reset();
#endif
	base = vfsmount_test_create(SB_KERNMOUNT);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(base));

	path.mnt = base;
	path.dentry = base->mnt_root;
	clone = mnt_clone_internal(&path);
	if (IS_ERR(clone)) {
		mntput(base);
		KUNIT_FAIL(test, "failed to clone internal mount: %ld",
			   PTR_ERR(clone));
		return;
	}

	KUNIT_EXPECT_PTR_EQ(test, base->mnt_root, clone->mnt_root);
	KUNIT_EXPECT_PTR_EQ(test, base->mnt_sb, clone->mnt_sb);

	mntput(clone);
	mntput(base);
	rcu_barrier();

#ifdef CONFIG_RUST_VFSMOUNT_LIFECYCLE
	KUNIT_EXPECT_EQ(test, 1,
			vfsmount_test_counter(VFSMOUNT_COUNTER_CLONE));
	KUNIT_EXPECT_EQ(test, 2,
			vfsmount_test_counter(VFSMOUNT_COUNTER_CLEANUP));
	KUNIT_EXPECT_EQ(test, 2, vfsmount_test_counter(VFSMOUNT_COUNTER_FREE));
#endif
}

#ifdef CONFIG_RUST_VFSMOUNT_LIFECYCLE
static void vfsmount_lifecycle_test_clone_shared(struct kunit *test)
{
	struct vfsmount *base;
	struct mount *base_mnt;
	struct mount *clone_mnt;
	struct vfsmount *clone;
	struct path path;

	rust_mnt_test_reset();
	base = vfsmount_test_create(SB_KERNMOUNT);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(base));

	base_mnt = rust_helper_mnt_from_vfsmount(base);
	path.mnt = base;
	path.dentry = base->mnt_root;
	clone_mnt = rust_mnt_test_clone(base_mnt, path.dentry,
					RUST_MNT_CL_MAKE_SHARED);
	if (IS_ERR(clone_mnt)) {
		mntput(base);
		KUNIT_FAIL(test, "failed to create shared clone: %ld",
			   PTR_ERR(clone_mnt));
		return;
	}

	clone = rust_helper_mnt_vfsmount(clone_mnt);
	KUNIT_EXPECT_GT(test, rust_helper_mnt_group_id(clone_mnt), 0);
	KUNIT_EXPECT_TRUE(test,
			  rust_helper_mnt_t_flags(clone_mnt) &
				  RUST_MNT_T_SHARED);
	rust_helper_mnt_test_release_group_id(clone_mnt);

	mntput(clone);
	mntput(base);
	vfsmount_lifecycle_test_flush();
	rcu_barrier();

	KUNIT_EXPECT_EQ(test, 1,
			vfsmount_test_counter(VFSMOUNT_COUNTER_CLONE));
	KUNIT_EXPECT_EQ(test, 2,
			vfsmount_test_counter(VFSMOUNT_COUNTER_CLEANUP));
	KUNIT_EXPECT_EQ(test, 2, vfsmount_test_counter(VFSMOUNT_COUNTER_FREE));
}
#endif

static void vfsmount_lifecycle_test_shortterm(struct kunit *test)
{
	struct vfsmount *mnt;

#ifdef CONFIG_RUST_VFSMOUNT_LIFECYCLE
	rust_mnt_test_reset();
#endif
	mnt = vfsmount_test_create(0);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(mnt));

	mnt_make_shortterm(mnt);
	mntput(mnt);
	task_work_run();
	vfsmount_lifecycle_test_flush();
	rcu_barrier();

#ifdef CONFIG_RUST_VFSMOUNT_LIFECYCLE
	KUNIT_EXPECT_EQ(test, 1,
			vfsmount_test_counter(VFSMOUNT_COUNTER_CLEANUP));
	KUNIT_EXPECT_EQ(test, 1, vfsmount_test_counter(VFSMOUNT_COUNTER_FREE));
#endif
}

static int vfsmount_test_put_worker(void *data)
{
	struct vfsmount_worker_context *ctx = data;

	mntput(ctx->mnt);
	complete(&ctx->done);
	return 0;
}

static void vfsmount_lifecycle_test_delayed_cleanup(struct kunit *test)
{
	struct vfsmount_worker_context ctx = {};
	struct task_struct *task;
	bool done;

#ifdef CONFIG_RUST_VFSMOUNT_LIFECYCLE
	rust_mnt_test_reset();
#endif
	ctx.mnt = vfsmount_test_create(0);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(ctx.mnt));
	init_completion(&ctx.done);

	task = kthread_run(vfsmount_test_put_worker, &ctx, "vfsmount-delayed");
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(task));
	get_task_struct(task);

	done = vfsmount_test_wait(&ctx.done);
	KUNIT_EXPECT_TRUE(test, done);
	KUNIT_EXPECT_EQ(test, 0, kthread_stop_put(task));
	vfsmount_lifecycle_test_flush();
	rcu_barrier();

#ifdef CONFIG_RUST_VFSMOUNT_LIFECYCLE
	KUNIT_EXPECT_EQ(test, 1,
			vfsmount_test_counter(VFSMOUNT_COUNTER_CLEANUP));
	KUNIT_EXPECT_EQ(test, 1, vfsmount_test_counter(VFSMOUNT_COUNTER_FREE));
	KUNIT_EXPECT_EQ(test, 1,
			vfsmount_test_counter(VFSMOUNT_COUNTER_DELAYED));
#endif
}

static void vfsmount_lifecycle_test_concurrent_release(struct kunit *test)
{
	struct vfsmount_worker_context contexts[VFSMOUNT_TEST_WORKERS] = {};
	struct task_struct *tasks[VFSMOUNT_TEST_WORKERS] = {};
	struct vfsmount *mnt;
	int started = 0;
	int index;
	bool timed_out = false;

#ifdef CONFIG_RUST_VFSMOUNT_LIFECYCLE
	rust_mnt_test_reset();
#endif
	mnt = vfsmount_test_create(0);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(mnt));

	for (index = 0; index < VFSMOUNT_TEST_WORKERS; index++) {
		contexts[index].mnt = mnt;
		init_completion(&contexts[index].done);
		if (index < VFSMOUNT_TEST_WORKERS - 1)
			mntget(mnt);
		tasks[index] = kthread_run(vfsmount_test_put_worker,
					   &contexts[index],
					   "vfsmount-concurrent-%d", index);
		if (IS_ERR_OR_NULL(tasks[index])) {
			KUNIT_FAIL(test, "failed to start worker %d", index);
			break;
		}
		get_task_struct(tasks[index]);
		started++;
	}

	for (index = 0; index < started; index++) {
		if (!vfsmount_test_wait(&contexts[index].done))
			timed_out = true;
	}
	for (index = 0; index < started; index++)
		KUNIT_EXPECT_EQ(test, 0, kthread_stop_put(tasks[index]));

	KUNIT_EXPECT_EQ(test, VFSMOUNT_TEST_WORKERS, started);
	KUNIT_EXPECT_FALSE(test, timed_out);
	vfsmount_lifecycle_test_flush();
	rcu_barrier();

#ifdef CONFIG_RUST_VFSMOUNT_LIFECYCLE
	KUNIT_EXPECT_EQ(test, 1,
			vfsmount_test_counter(VFSMOUNT_COUNTER_CLEANUP));
	KUNIT_EXPECT_EQ(test, 1, vfsmount_test_counter(VFSMOUNT_COUNTER_FREE));
	KUNIT_EXPECT_EQ(test, 1,
			vfsmount_test_counter(VFSMOUNT_COUNTER_DELAYED));
#endif
}

#ifdef CONFIG_RUST_VFSMOUNT_LIFECYCLE
static void vfsmount_lifecycle_test_allocation_rollback(struct kunit *test)
{
	struct mount *mnt;
	unsigned int stage;

	rust_mnt_test_reset();
	for (stage = RUST_MNT_TEST_FAIL_ALLOC;
	     stage <= RUST_MNT_TEST_FAIL_PCP; stage++) {
		unsigned int expected;

		rust_mnt_test_fail_next(stage);
		mnt = rust_mnt_test_alloc(NULL);
		KUNIT_EXPECT_NULL(test, mnt);
		expected = stage - RUST_MNT_TEST_FAIL_ALLOC + 1;
		KUNIT_EXPECT_EQ(test, expected,
				vfsmount_test_counter(VFSMOUNT_COUNTER_ALLOC));
		KUNIT_EXPECT_EQ(test, expected,
				vfsmount_test_counter(
					VFSMOUNT_COUNTER_ALLOC_FAILURE));
	}
	KUNIT_EXPECT_EQ(test,
			RUST_MNT_TEST_FAIL_PCP - RUST_MNT_TEST_FAIL_ALLOC,
			vfsmount_test_counter(VFSMOUNT_COUNTER_FREE));

	rust_mnt_test_reset();
	mnt = rust_mnt_test_alloc(NULL);
	KUNIT_EXPECT_EQ(test, -EINVAL, PTR_ERR(mnt));
	KUNIT_EXPECT_EQ(test, 0,
			vfsmount_test_counter(VFSMOUNT_COUNTER_ALLOC));
	rust_mnt_test_reset();
}

static void vfsmount_lifecycle_test_clone_rollback(struct kunit *test)
{
	struct vfsmount *base;
	struct mount *base_mnt;
	struct mount *clone;
	struct path path;

	base = vfsmount_test_create(SB_KERNMOUNT);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(base));
	base_mnt = rust_helper_mnt_from_vfsmount(base);
	path.mnt = base;
	path.dentry = base->mnt_root;

	rust_mnt_test_reset();
	rust_mnt_test_fail_next(RUST_MNT_TEST_FAIL_GROUP);
	clone = rust_mnt_test_clone(base_mnt, path.dentry,
				    RUST_MNT_CL_MAKE_SHARED);
	KUNIT_EXPECT_TRUE(test, IS_ERR(clone));
	KUNIT_EXPECT_EQ(test, -ENOMEM, PTR_ERR(clone));
	KUNIT_EXPECT_EQ(test, 1, vfsmount_test_counter(VFSMOUNT_COUNTER_ALLOC));
	KUNIT_EXPECT_EQ(test, 1,
			vfsmount_test_counter(VFSMOUNT_COUNTER_ALLOC_FAILURE));
	KUNIT_EXPECT_EQ(test, 1, vfsmount_test_counter(VFSMOUNT_COUNTER_FREE));

	rust_mnt_test_reset();
	mntput(base);
	rcu_barrier();
}
#endif

static void vfsmount_lifecycle_test_path_integration(struct kunit *test)
{
	struct vfsmount *mnt;
	struct path resolved;
	int error;

#ifdef CONFIG_RUST_VFSMOUNT_LIFECYCLE
	rust_mnt_test_reset();
#endif
	mnt = vfsmount_test_create(SB_KERNMOUNT);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(mnt));

	error = vfs_path_lookup(mnt->mnt_root, mnt, ".",
				LOOKUP_DIRECTORY, &resolved);
	KUNIT_ASSERT_EQ(test, 0, error);
	KUNIT_EXPECT_PTR_EQ(test, mnt, resolved.mnt);
	KUNIT_EXPECT_PTR_EQ(test, mnt->mnt_root, resolved.dentry);
	path_put(&resolved);

	mntput(mnt);
	rcu_barrier();

#ifdef CONFIG_RUST_VFSMOUNT_LIFECYCLE
	KUNIT_EXPECT_EQ(test, 1,
			vfsmount_test_counter(VFSMOUNT_COUNTER_CLEANUP));
	KUNIT_EXPECT_EQ(test, 1, vfsmount_test_counter(VFSMOUNT_COUNTER_FREE));
#endif
}

static struct kunit_case vfsmount_lifecycle_test_cases[] = {
	KUNIT_CASE(vfsmount_lifecycle_test_null_operations),
	KUNIT_CASE(vfsmount_lifecycle_test_create_final),
	KUNIT_CASE(vfsmount_lifecycle_test_shared_references),
	KUNIT_CASE(vfsmount_lifecycle_test_clone_internal),
	KUNIT_CASE(vfsmount_lifecycle_test_shortterm),
	KUNIT_CASE(vfsmount_lifecycle_test_delayed_cleanup),
	KUNIT_CASE(vfsmount_lifecycle_test_concurrent_release),
	KUNIT_CASE(vfsmount_lifecycle_test_path_integration),
#ifdef CONFIG_RUST_VFSMOUNT_LIFECYCLE
	KUNIT_CASE(vfsmount_lifecycle_test_clone_shared),
	KUNIT_CASE(vfsmount_lifecycle_test_allocation_rollback),
	KUNIT_CASE(vfsmount_lifecycle_test_clone_rollback),
#endif
	{}
};

static int vfsmount_lifecycle_suite_init(struct kunit_suite *suite)
{
	return register_filesystem(&vfsmount_test_fs_type);
}

static void vfsmount_lifecycle_suite_exit(struct kunit_suite *suite)
{
	vfsmount_lifecycle_test_flush();
	rcu_barrier();
	unregister_filesystem(&vfsmount_test_fs_type);
}

static struct kunit_suite vfsmount_lifecycle_test_suite = {
	.name = "vfsmount_lifecycle",
	.suite_init = vfsmount_lifecycle_suite_init,
	.suite_exit = vfsmount_lifecycle_suite_exit,
	.test_cases = vfsmount_lifecycle_test_cases,
};

kunit_test_suite(vfsmount_lifecycle_test_suite);

MODULE_DESCRIPTION("KUnit tests for vfsmount lifecycle");
MODULE_LICENSE("GPL");
