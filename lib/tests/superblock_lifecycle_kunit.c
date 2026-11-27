// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>
#include <linux/atomic.h>
#include <linux/completion.h>
#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/rcupdate.h>
#include <linux/sched/task.h>
#include <linux/superblock-rust.h>
#include <linux/wait_bit.h>
#include <linux/workqueue.h>

#define SUPER_TEST_TIMEOUT		(30 * HZ)
#define SUPER_TEST_WORKERS		4
#define SUPER_TEST_ITERATIONS		256

#define SUPER_COUNTER_ALLOC		0
#define SUPER_COUNTER_ALLOC_FAILURE	1
#define SUPER_COUNTER_DESTROY_WORK	2
#define SUPER_COUNTER_UNUSED		3
#define SUPER_COUNTER_SGET_NEW		4
#define SUPER_COUNTER_SGET_SHARE		5
#define SUPER_COUNTER_KILL_NOTIFY	6

#define SUPER_FAIL_ALLOC		1
#define SUPER_FAIL_INIT			2
#define SUPER_FAIL_SECURITY		3
#define SUPER_FAIL_WRITER_BASE		4
#define SUPER_FAIL_SHRINKER		(SUPER_FAIL_WRITER_BASE + SB_FREEZE_LEVELS)
#define SUPER_FAIL_DENTRY_LRU		(SUPER_FAIL_SHRINKER + 1)
#define SUPER_FAIL_INODE_LRU		(SUPER_FAIL_DENTRY_LRU + 1)
#define SUPER_FAIL_DEV			(SUPER_FAIL_INODE_LRU + 1)

struct super_worker_context {
	struct completion done;
	atomic_t errors;
	atomic_t iterations;
};

static int super_test_init_fs_context(struct fs_context *fc)
{
	static const struct fs_context_operations ops;

	fc->ops = &ops;
	return 0;
}

static struct file_system_type super_test_fs_type = {
	.name = "superblock_lifecycle_test",
	.init_fs_context = super_test_init_fs_context,
	.kill_sb = kill_anon_super,
	.owner = THIS_MODULE,
};

static int super_test_set(struct super_block *sb, struct fs_context *fc)
{
	return set_anon_super_fc(sb, fc);
}

static int super_test_set_fail(struct super_block *sb, struct fs_context *fc)
{
	return -EIO;
}

static int super_test_always_match(struct super_block *sb,
				   struct fs_context *fc)
{
	return 1;
}

static void super_test_mark_born(struct super_block *sb)
{
	unsigned long flags = READ_ONCE(sb->s_flags) | SB_BORN;

	/* Publish the filesystem state before waking mount waiters. */
	smp_store_release(&sb->s_flags, flags);
	/* Order the state publication before the waitvar wake-up. */
	smp_mb();
	wake_up_var(&sb->s_flags);
}

static void super_test_drain(void)
{
	rcu_barrier();
	flush_workqueue(system_wq);
}

static struct fs_context *super_test_context(struct kunit *test)
{
	struct fs_context *fc;

	fc = fs_context_for_mount(&super_test_fs_type, 0);
	KUNIT_ASSERT_FALSE(test, IS_ERR(fc));
	return fc;
}

#ifdef CONFIG_RUST_SUPERBLOCK_LIFECYCLE
static unsigned int super_test_counter(unsigned int index)
{
	return rust_super_test_counter(index);
}
#endif

static void superblock_lifecycle_test_create_final(struct kunit *test)
{
	struct fs_context *fc = super_test_context(test);
	struct super_block *sb;

	sb = sget_fc(fc, NULL, super_test_set);
	KUNIT_ASSERT_FALSE(test, IS_ERR(sb));
	KUNIT_EXPECT_PTR_EQ(test, &super_test_fs_type, sb->s_type);
	KUNIT_EXPECT_STREQ(test, super_test_fs_type.name, sb->s_id);

	deactivate_locked_super(sb);
	put_fs_context(fc);
	super_test_drain();
}

static void superblock_lifecycle_test_shared_references(struct kunit *test)
{
	struct fs_context *fc1 = super_test_context(test);
	struct fs_context *fc2 = super_test_context(test);
	struct super_block *sb1;
	struct super_block *sb2;

	sb1 = sget_fc(fc1, NULL, super_test_set);
	KUNIT_ASSERT_FALSE(test, IS_ERR(sb1));
	super_test_mark_born(sb1);
	up_write(&sb1->s_umount);

	sb2 = sget_fc(fc2, super_test_always_match, super_test_set);
	KUNIT_ASSERT_FALSE(test, IS_ERR(sb2));
	KUNIT_EXPECT_PTR_EQ(test, sb1, sb2);
	KUNIT_EXPECT_EQ(test, 2, atomic_read(&sb2->s_active));

	deactivate_locked_super(sb2);
	KUNIT_EXPECT_EQ(test, 1, atomic_read(&sb1->s_active));
	deactivate_super(sb1);

	put_fs_context(fc1);
	put_fs_context(fc2);
	super_test_drain();
}

static void superblock_lifecycle_test_setup_failure(struct kunit *test)
{
	struct fs_context *fc = super_test_context(test);
	struct super_block *sb;

	sb = sget_fc(fc, NULL, super_test_set_fail);
	KUNIT_EXPECT_TRUE(test, IS_ERR(sb));
	KUNIT_EXPECT_EQ(test, -EIO, PTR_ERR(sb));

	put_fs_context(fc);
	super_test_drain();
}

static void superblock_lifecycle_test_exclusive_reuse(struct kunit *test)
{
	struct fs_context *fc1 = super_test_context(test);
	struct fs_context *fc2 = super_test_context(test);
	struct super_block *original_sb;
	struct super_block *sb;

	original_sb = sget_fc(fc1, NULL, super_test_set);
	KUNIT_ASSERT_FALSE(test, IS_ERR(original_sb));
	super_test_mark_born(original_sb);
	up_write(&original_sb->s_umount);

	fc2->exclusive = true;
	sb = sget_fc(fc2, super_test_always_match, super_test_set);
	KUNIT_EXPECT_TRUE(test, IS_ERR(sb));
	KUNIT_EXPECT_EQ(test, -EBUSY, PTR_ERR(sb));

	deactivate_super(original_sb);
	put_fs_context(fc1);
	put_fs_context(fc2);
	super_test_drain();
}

static int super_worker_thread(void *data)
{
	struct super_worker_context *ctx = data;
	int index;

	for (index = 0; index < SUPER_TEST_ITERATIONS; index++) {
		struct fs_context *fc;
		struct super_block *sb;

		fc = fs_context_for_mount(&super_test_fs_type, 0);
		if (IS_ERR(fc)) {
			atomic_inc(&ctx->errors);
			break;
		}
		sb = sget_fc(fc, super_test_always_match, super_test_set);
		if (IS_ERR(sb)) {
			atomic_inc(&ctx->errors);
		} else {
			super_test_mark_born(sb);
			deactivate_locked_super(sb);
		}
		put_fs_context(fc);
		atomic_inc(&ctx->iterations);
		cond_resched();
	}
	complete(&ctx->done);
	return 0;
}

static void superblock_lifecycle_test_concurrent(struct kunit *test)
{
	struct super_worker_context contexts[SUPER_TEST_WORKERS];
	struct task_struct *tasks[SUPER_TEST_WORKERS];
	bool timed_out = false;
	int started = 0;
	int index;

	for (index = 0; index < SUPER_TEST_WORKERS; index++) {
		init_completion(&contexts[index].done);
		atomic_set(&contexts[index].errors, 0);
		atomic_set(&contexts[index].iterations, 0);
		tasks[index] = kthread_run(super_worker_thread, &contexts[index],
					   "super-worker-%d", index);
		if (IS_ERR(tasks[index])) {
			KUNIT_FAIL(test, "failed to start worker %d", index);
			break;
		}
		get_task_struct(tasks[index]);
		started++;
	}

	for (index = 0; index < started; index++) {
		if (!wait_for_completion_timeout(&contexts[index].done,
						 SUPER_TEST_TIMEOUT))
			timed_out = true;
	}
	for (index = 0; index < started; index++) {
		KUNIT_EXPECT_EQ(test, 0, kthread_stop_put(tasks[index]));
		KUNIT_EXPECT_EQ(test, 0, atomic_read(&contexts[index].errors));
		KUNIT_EXPECT_EQ(test, SUPER_TEST_ITERATIONS,
				atomic_read(&contexts[index].iterations));
	}
	KUNIT_EXPECT_EQ(test, SUPER_TEST_WORKERS, started);
	KUNIT_EXPECT_FALSE(test, timed_out);
	super_test_drain();
}

#ifdef CONFIG_RUST_SUPERBLOCK_LIFECYCLE
static void superblock_lifecycle_test_alloc_failure(struct kunit *test)
{
	struct fs_context *fc = super_test_context(test);
	struct super_block *sb;

	rust_super_test_reset();
	rust_super_test_fail(SUPER_FAIL_ALLOC);
	sb = sget_fc(fc, NULL, super_test_set);
	KUNIT_EXPECT_TRUE(test, IS_ERR(sb));
	KUNIT_EXPECT_EQ(test, -ENOMEM, PTR_ERR(sb));
	KUNIT_EXPECT_EQ(test, 1, super_test_counter(SUPER_COUNTER_ALLOC));
	KUNIT_EXPECT_EQ(test, 1,
			super_test_counter(SUPER_COUNTER_ALLOC_FAILURE));

	rust_super_test_reset();
	put_fs_context(fc);
}

static void superblock_lifecycle_test_init_rollback(struct kunit *test)
{
	struct fs_context *fc = super_test_context(test);
	struct super_block *sb;
	unsigned int stage;

	for (stage = SUPER_FAIL_INIT; stage <= SUPER_FAIL_DEV; stage++) {
		rust_super_test_reset();
		rust_super_test_fail(stage);
		sb = sget_fc(fc, NULL, super_test_set);
		KUNIT_EXPECT_TRUE(test, IS_ERR(sb));
		KUNIT_EXPECT_EQ(test, -ENOMEM, PTR_ERR(sb));
		KUNIT_EXPECT_EQ(test, 1, super_test_counter(SUPER_COUNTER_ALLOC));
		KUNIT_EXPECT_EQ(test, 1,
				super_test_counter(SUPER_COUNTER_ALLOC_FAILURE));
		KUNIT_EXPECT_EQ(test, 1, super_test_counter(SUPER_COUNTER_UNUSED));
		KUNIT_EXPECT_EQ(test, 1,
				super_test_counter(SUPER_COUNTER_DESTROY_WORK));
	}

	rust_super_test_reset();
	put_fs_context(fc);
}
#endif

static struct kunit_case superblock_lifecycle_test_cases[] = {
	KUNIT_CASE(superblock_lifecycle_test_create_final),
	KUNIT_CASE(superblock_lifecycle_test_shared_references),
	KUNIT_CASE(superblock_lifecycle_test_setup_failure),
	KUNIT_CASE(superblock_lifecycle_test_exclusive_reuse),
	KUNIT_CASE(superblock_lifecycle_test_concurrent),
#ifdef CONFIG_RUST_SUPERBLOCK_LIFECYCLE
	KUNIT_CASE(superblock_lifecycle_test_alloc_failure),
	KUNIT_CASE(superblock_lifecycle_test_init_rollback),
#endif
	{}
};

static int superblock_lifecycle_suite_init(struct kunit_suite *suite)
{
	INIT_HLIST_HEAD(&super_test_fs_type.fs_supers);
	return register_filesystem(&super_test_fs_type);
}

static void superblock_lifecycle_suite_exit(struct kunit_suite *suite)
{
	super_test_drain();
	unregister_filesystem(&super_test_fs_type);
}

static struct kunit_suite superblock_lifecycle_test_suite = {
	.name = "superblock_lifecycle",
	.suite_init = superblock_lifecycle_suite_init,
	.suite_exit = superblock_lifecycle_suite_exit,
	.test_cases = superblock_lifecycle_test_cases,
};

kunit_test_suite(superblock_lifecycle_test_suite);

MODULE_DESCRIPTION("KUnit tests for superblock lifecycle");
MODULE_LICENSE("GPL");
