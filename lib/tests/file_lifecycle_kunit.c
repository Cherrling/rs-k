// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>
#include <linux/anon_inodes.h>
#include <linux/completion.h>
#include <linux/cred.h>
#include <linux/err.h>
#include <linux/file.h>
#include <linux/file-lifecycle-rust.h>
#include <linux/fs.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/task_work.h>

#define FILE_LIFECYCLE_TEST_TIMEOUT	(5 * HZ)
#define FILE_LIFECYCLE_TEST_WORKERS	4

struct file_fput_context {
	struct completion done;
	struct file *file;
};

static atomic_t file_release_count;
static atomic_t file_fasync_count;
#ifdef CONFIG_RUST_FILE_LIFECYCLE
static atomic_t file_release_free_count;
#endif

static int file_lifecycle_test_release(struct inode *inode, struct file *file)
{
#ifdef CONFIG_RUST_FILE_LIFECYCLE
	atomic_set(&file_release_free_count, rust_file_test_free_count());
#endif
	atomic_inc(&file_release_count);
	return 0;
}

static int file_lifecycle_test_fasync(int fd, struct file *file, int on)
{
	atomic_inc(&file_fasync_count);
	return 0;
}

static const struct file_operations file_lifecycle_test_fops = {
	.release = file_lifecycle_test_release,
	.fasync = file_lifecycle_test_fasync,
};

static void file_lifecycle_test_reset_counters(void)
{
	atomic_set(&file_release_count, 0);
	atomic_set(&file_fasync_count, 0);
#ifdef CONFIG_RUST_FILE_LIFECYCLE
	atomic_set(&file_release_free_count, -1);
#endif
}

static struct file *file_lifecycle_test_create(int flags)
{
	return anon_inode_getfile("file-lifecycle",
				  &file_lifecycle_test_fops, NULL, flags);
}

static bool file_lifecycle_test_wait(struct completion *completion)
{
	return wait_for_completion_timeout(completion,
					   FILE_LIFECYCLE_TEST_TIMEOUT);
}

static void file_lifecycle_test_empty_file(struct kunit *test)
{
	struct cred *cred = (struct cred *)current_cred();
	struct file *file;
#ifdef CONFIG_RUST_FILE_LIFECYCLE
	u32 free_count = rust_file_test_free_count();
	u32 init_count = rust_file_test_init_count();
#endif

	file = file_lifecycle_test_alloc_empty(O_RDONLY, cred);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(file));
	KUNIT_EXPECT_FALSE(test, file->f_mode & FMODE_OPENED);
	KUNIT_EXPECT_PTR_EQ(test, file->f_cred, cred);
	KUNIT_EXPECT_EQ(test, O_RDONLY, file->f_flags & O_ACCMODE);
	__fput_sync(file);
#ifdef CONFIG_RUST_FILE_LIFECYCLE
	KUNIT_EXPECT_GT(test, rust_file_test_free_count(), free_count);
	KUNIT_EXPECT_GT(test, rust_file_test_init_count(), init_count);
#endif
}

static void file_lifecycle_test_pseudo_release(struct kunit *test)
{
	struct file *file;
#ifdef CONFIG_RUST_FILE_LIFECYCLE
	u32 free_count = rust_file_test_free_count();
	u32 release_count = rust_file_test_release_count();
	u32 path_count = rust_file_test_path_count();
#endif

	file_lifecycle_test_reset_counters();
	file = file_lifecycle_test_create(O_RDONLY);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(file));
	KUNIT_ASSERT_NOT_NULL(test, file->f_path.dentry);
	KUNIT_ASSERT_NOT_NULL(test, file->f_path.mnt);
	file->f_flags |= FASYNC;

	fput_close_sync(file);
	KUNIT_EXPECT_EQ(test, 1, atomic_read(&file_release_count));
	KUNIT_EXPECT_EQ(test, 1, atomic_read(&file_fasync_count));
#ifdef CONFIG_RUST_FILE_LIFECYCLE
	KUNIT_EXPECT_GT(test, rust_file_test_release_count(), release_count);
	KUNIT_EXPECT_GT(test, rust_file_test_path_count(), path_count);
	KUNIT_EXPECT_EQ(test, free_count, atomic_read(&file_release_free_count));
	KUNIT_EXPECT_EQ(test, free_count + 1, rust_file_test_free_count());
#endif
}

static void file_lifecycle_test_clone(struct kunit *test)
{
	struct file *base;
	struct file *clone;

	base = file_lifecycle_test_create(O_RDONLY);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(base));

	clone = alloc_file_clone(base, O_RDONLY,
				 &file_lifecycle_test_fops);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(clone));
	KUNIT_EXPECT_PTR_EQ(test, base->f_path.dentry, clone->f_path.dentry);
	KUNIT_EXPECT_PTR_EQ(test, base->f_path.mnt, clone->f_path.mnt);
	KUNIT_EXPECT_PTR_EQ(test, base->f_mapping, clone->f_mapping);

	__fput_sync(clone);
	__fput_sync(base);
}

static void file_lifecycle_test_fput(struct kunit *test)
{
	struct file *file;

	file_lifecycle_test_reset_counters();
	file = file_lifecycle_test_create(O_RDONLY);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(file));

	get_file(file);
	get_file(file);
	fput(file);
	fput(file);
	KUNIT_EXPECT_EQ(test, 0, atomic_read(&file_release_count));
	__fput_sync(file);
	KUNIT_EXPECT_EQ(test, 1, atomic_read(&file_release_count));
}

#ifdef CONFIG_RUST_FILE_LIFECYCLE
static void file_lifecycle_test_failure_injection(struct kunit *test)
{
	static const struct {
		u32 stage;
		int error;
		bool pseudo;
	} stages[] = {
		{ RUST_FILE_TEST_FAIL_LIMIT, -ENFILE, false },
		{ RUST_FILE_TEST_FAIL_ALLOC, -ENOMEM, false },
		{ RUST_FILE_TEST_FAIL_INIT, -ENOMEM, false },
		{ RUST_FILE_TEST_FAIL_PSEUDO_PATH, -ENOMEM, true },
	};
	const struct cred *cred = current_cred();
	u32 failures = rust_file_test_failure_count();
	unsigned int index;

	for (index = 0; index < ARRAY_SIZE(stages); index++) {
		struct file *file;

		rust_file_test_fail_next(stages[index].stage);
		if (stages[index].pseudo)
			file = file_lifecycle_test_create(O_RDONLY);
		else
			file = file_lifecycle_test_alloc_empty(O_RDONLY, cred);
		KUNIT_EXPECT_TRUE(test, IS_ERR(file));
		KUNIT_EXPECT_EQ(test, stages[index].error, PTR_ERR(file));
	}

	rust_file_test_fail_next(0);
	KUNIT_EXPECT_EQ(test, failures + ARRAY_SIZE(stages),
			rust_file_test_failure_count());
}
#endif

static int file_lifecycle_test_delayed_worker(void *data)
{
	struct file_fput_context *context = data;

	fput_close(context->file);
	complete(&context->done);
	while (!kthread_should_stop())
		schedule();
	return 0;
}

static void file_lifecycle_test_delayed_fput(struct kunit *test)
{
	struct file_fput_context context = {};
	struct task_struct *task;
	bool done;
#ifdef CONFIG_RUST_FILE_LIFECYCLE
	u32 delayed_count = rust_file_test_delayed_count();
	u32 free_count = rust_file_test_free_count();
	u32 release_count = rust_file_test_release_count();
#endif

	file_lifecycle_test_reset_counters();
	context.file = file_lifecycle_test_create(O_RDONLY);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(context.file));
	init_completion(&context.done);
	task = kthread_run(file_lifecycle_test_delayed_worker, &context,
			   "file-delayed-fput");
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(task));
	get_task_struct(task);

	done = file_lifecycle_test_wait(&context.done);
	KUNIT_EXPECT_TRUE(test, done);
	KUNIT_EXPECT_EQ(test, 0, kthread_stop_put(task));
	flush_delayed_fput();

	KUNIT_EXPECT_EQ(test, 1, atomic_read(&file_release_count));
#ifdef CONFIG_RUST_FILE_LIFECYCLE
	KUNIT_EXPECT_GT(test, rust_file_test_delayed_count(), delayed_count);
	KUNIT_EXPECT_GT(test, rust_file_test_release_count(), release_count);
	KUNIT_EXPECT_GT(test, rust_file_test_free_count(), free_count);
#endif
}

#ifdef CONFIG_RUST_FILE_LIFECYCLE
static void file_lifecycle_test_task_work(struct kunit *test)
{
	struct file *file;
	u32 free_count = rust_file_test_free_count();
	u32 release_count = rust_file_test_release_count();
	u32 task_work_count = rust_file_test_task_work_count();

	file_lifecycle_test_reset_counters();
	file = file_lifecycle_test_create(O_RDONLY);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(file));

	KUNIT_ASSERT_TRUE(test, rust_file_test_queue_task_work(file));
	task_work_run();
	KUNIT_EXPECT_EQ(test, 1, atomic_read(&file_release_count));
	KUNIT_EXPECT_GT(test, rust_file_test_task_work_count(), task_work_count);
	KUNIT_EXPECT_GT(test, rust_file_test_release_count(), release_count);
	KUNIT_EXPECT_GT(test, rust_file_test_free_count(), free_count);
}
#endif

static int file_lifecycle_test_fput_worker(void *data)
{
	struct file_fput_context *context = data;

	fput(context->file);
	complete(&context->done);
	while (!kthread_should_stop())
		schedule();
	return 0;
}

static void file_lifecycle_test_concurrent_fput(struct kunit *test)
{
	struct file_fput_context contexts[FILE_LIFECYCLE_TEST_WORKERS] = {};
	struct task_struct *tasks[FILE_LIFECYCLE_TEST_WORKERS] = {};
	struct file *file;
	unsigned int index;
	bool timed_out = false;

	file_lifecycle_test_reset_counters();
	file = file_lifecycle_test_create(O_RDONLY);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(file));

	for (index = 0; index < FILE_LIFECYCLE_TEST_WORKERS; index++) {
		get_file(file);
		contexts[index].file = file;
		init_completion(&contexts[index].done);
		tasks[index] = kthread_run(file_lifecycle_test_fput_worker,
					   &contexts[index],
					   "file-concurrent-fput-%u", index);
		KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(tasks[index]));
		get_task_struct(tasks[index]);
	}

	for (index = 0; index < FILE_LIFECYCLE_TEST_WORKERS; index++) {
		if (!file_lifecycle_test_wait(&contexts[index].done))
			timed_out = true;
	}
	for (index = 0; index < FILE_LIFECYCLE_TEST_WORKERS; index++)
		KUNIT_EXPECT_EQ(test, 0, kthread_stop_put(tasks[index]));

	KUNIT_EXPECT_FALSE(test, timed_out);
	KUNIT_EXPECT_EQ(test, 0, atomic_read(&file_release_count));
	__fput_sync(file);
	flush_delayed_fput();
	KUNIT_EXPECT_EQ(test, 1, atomic_read(&file_release_count));
}

static struct kunit_case file_lifecycle_test_cases[] = {
#ifdef CONFIG_RUST_FILE_LIFECYCLE
	KUNIT_CASE(file_lifecycle_test_failure_injection),
	KUNIT_CASE(file_lifecycle_test_task_work),
#endif
	KUNIT_CASE(file_lifecycle_test_empty_file),
	KUNIT_CASE(file_lifecycle_test_pseudo_release),
	KUNIT_CASE(file_lifecycle_test_clone),
	KUNIT_CASE(file_lifecycle_test_fput),
	KUNIT_CASE(file_lifecycle_test_delayed_fput),
	KUNIT_CASE(file_lifecycle_test_concurrent_fput),
	{}
};

static struct kunit_suite file_lifecycle_test_suite = {
	.name = "file_lifecycle",
	.test_cases = file_lifecycle_test_cases,
};

kunit_test_suite(file_lifecycle_test_suite);

MODULE_DESCRIPTION("KUnit tests for file lifecycle");
MODULE_LICENSE("GPL");
