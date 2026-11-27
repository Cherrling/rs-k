// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>
#include <linux/anon_inodes.h>
#include <linux/completion.h>
#include <linux/fdtable.h>
#include <linux/fdtable-rust.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/task_work.h>

#define FDTABLE_TEST_TIMEOUT		(5 * HZ)
#define FDTABLE_TEST_EXPAND_FDS		128
#define FDTABLE_TEST_WORKERS		4
#define FDTABLE_TEST_LOOKUPS		8192

struct fdtable_reader_context {
	struct completion started;
	struct completion done;
	struct files_struct *files;
	struct file *file;
	unsigned int fd;
	atomic_t stop;
	atomic_t lookups;
	atomic_t errors;
};

static atomic_t fdtable_release_count;

static int fdtable_test_release(struct inode *inode, struct file *file)
{
	atomic_inc(&fdtable_release_count);
	return 0;
}

static const struct file_operations fdtable_test_fops = {
	.release = fdtable_test_release,
};

static struct file *fdtable_test_create_file(void)
{
	return anon_inode_getfile("fdtable-lifecycle",
				  &fdtable_test_fops, NULL, O_RDONLY);
}

#ifdef CONFIG_RUST_FDTABLE_LIFECYCLE
static bool fdtable_test_wait(struct completion *completion)
{
	return wait_for_completion_timeout(completion, FDTABLE_TEST_TIMEOUT);
}
#endif

static void fdtable_test_install_file(struct kunit *test, struct file *file,
				      unsigned int *fd)
{
	int installed;

	installed = get_unused_fd_flags(0);
	KUNIT_ASSERT_GE(test, installed, 0);
	*fd = installed;
	fd_install(*fd, file);
}

static void fdtable_lifecycle_test_lookup_close(struct kunit *test)
{
	struct file *file;
	struct file *looked_up;
	struct fd light;
	unsigned int fd;
	unsigned int next_fd;

	atomic_set(&fdtable_release_count, 0);
	file = fdtable_test_create_file();
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(file));
	fdtable_test_install_file(test, file, &fd);

	looked_up = fget(fd);
	KUNIT_EXPECT_PTR_EQ(test, file, looked_up);
	if (looked_up)
		fput(looked_up);

	looked_up = fget_raw(fd);
	KUNIT_EXPECT_PTR_EQ(test, file, looked_up);
	if (looked_up)
		fput(looked_up);

	looked_up = fget_task(current, fd);
	KUNIT_EXPECT_PTR_EQ(test, file, looked_up);
	if (looked_up)
		fput(looked_up);

	next_fd = fd;
	looked_up = fget_task_next(current, &next_fd);
	KUNIT_EXPECT_FALSE(test, IS_ERR_OR_NULL(looked_up));
	KUNIT_EXPECT_GE(test, next_fd, fd);
	if (looked_up)
		fput(looked_up);

	light = fdget(fd);
	KUNIT_EXPECT_PTR_EQ(test, file, fd_file(light));
	fdput(light);

	KUNIT_EXPECT_EQ(test, 0, close_fd(fd));
	task_work_run();
	flush_delayed_fput();
	KUNIT_EXPECT_EQ(test, 1, atomic_read(&fdtable_release_count));
}

static void fdtable_lifecycle_test_expand(struct kunit *test)
{
	struct files_struct *files = current->files;
	unsigned int old_max;
	unsigned int fds[FDTABLE_TEST_EXPAND_FDS];
	unsigned int index;
	unsigned int installed = 0;

	atomic_set(&fdtable_release_count, 0);
	rcu_read_lock();
	old_max = files_fdtable(files)->max_fds;
	rcu_read_unlock();
	KUNIT_ASSERT_GE(test, rlimit(RLIMIT_NOFILE),
			FDTABLE_TEST_EXPAND_FDS + 16);

	for (index = 0; index < FDTABLE_TEST_EXPAND_FDS; index++) {
		struct file *file = fdtable_test_create_file();

		KUNIT_EXPECT_FALSE(test, IS_ERR_OR_NULL(file));
		if (IS_ERR_OR_NULL(file))
			break;

		installed = get_unused_fd_flags(0);
		KUNIT_EXPECT_GE(test, installed, 0);
		if (installed < 0) {
			fput(file);
			break;
		}
		fds[index] = installed;
		fd_install(installed, file);
		installed++;
	}

	KUNIT_EXPECT_EQ(test, FDTABLE_TEST_EXPAND_FDS, installed);
	rcu_read_lock();
	KUNIT_EXPECT_GT(test, files_fdtable(files)->max_fds, old_max);
	rcu_read_unlock();

	for (index = 0; index < installed; index++)
		KUNIT_EXPECT_EQ(test, 0, close_fd(fds[index]));

	task_work_run();
	flush_delayed_fput();
	KUNIT_EXPECT_EQ(test, FDTABLE_TEST_EXPAND_FDS,
			atomic_read(&fdtable_release_count));
}

static void fdtable_lifecycle_test_dup_release(struct kunit *test)
{
	struct files_struct *clone;
	struct file *file;
	unsigned int fd;
#ifdef CONFIG_RUST_FDTABLE_LIFECYCLE
	u32 closes = rust_fdtable_test_close_count();
#endif

	atomic_set(&fdtable_release_count, 0);
	file = fdtable_test_create_file();
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(file));
	fdtable_test_install_file(test, file, &fd);

	clone = dup_fd(current->files, NULL);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(clone));
	KUNIT_EXPECT_EQ(test, 1, atomic_read(&clone->count));
	KUNIT_EXPECT_PTR_NE(test, current->files, clone);

	put_files_struct(clone);
#ifdef CONFIG_RUST_FDTABLE_LIFECYCLE
	KUNIT_EXPECT_GT(test, rust_fdtable_test_close_count(), closes);
#endif
	KUNIT_EXPECT_EQ(test, 0, atomic_read(&fdtable_release_count));

	KUNIT_EXPECT_EQ(test, 0, close_fd(fd));
	task_work_run();
	flush_delayed_fput();
	KUNIT_EXPECT_EQ(test, 1, atomic_read(&fdtable_release_count));
}

#ifdef CONFIG_RUST_FDTABLE_LIFECYCLE
static void fdtable_lifecycle_test_fdget_state(struct kunit *test)
{
	struct files_struct *clone;
	struct file *file;
	struct fd light;
	unsigned int fd;

	atomic_set(&fdtable_release_count, 0);
	file = fdtable_test_create_file();
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(file));
	fdtable_test_install_file(test, file, &fd);

	clone = dup_fd(current->files, NULL);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(clone));
	KUNIT_EXPECT_EQ(test, 1, atomic_read(&clone->count));

	light = rust_fdtable_test_fdget(clone, fd, 0);
	KUNIT_EXPECT_PTR_EQ(test, file, fd_file(light));
	KUNIT_EXPECT_EQ(test, 0, light.word & FDPUT_FPUT);
	fdput(light);

	atomic_inc(&clone->count);
	light = rust_fdtable_test_fdget(clone, fd, 0);
	KUNIT_EXPECT_PTR_EQ(test, file, fd_file(light));
	KUNIT_EXPECT_NE(test, 0, light.word & FDPUT_FPUT);
	fdput(light);

	put_files_struct(clone);
	put_files_struct(clone);

	KUNIT_EXPECT_EQ(test, 0, close_fd(fd));
	task_work_run();
	flush_delayed_fput();
	KUNIT_EXPECT_EQ(test, 1, atomic_read(&fdtable_release_count));
}

static void fdtable_lifecycle_test_failure_injection(struct kunit *test)
{
	struct files_struct *clone;
	unsigned int max_fds;
	u32 failures = rust_fdtable_test_failure_count();

	rcu_read_lock();
	max_fds = files_fdtable(current->files)->max_fds;
	rcu_read_unlock();
	rust_fdtable_test_fail_next(RUST_FDTABLE_TEST_FAIL_FILES);
	clone = dup_fd(current->files, NULL);
	KUNIT_EXPECT_TRUE(test, IS_ERR(clone));
	KUNIT_EXPECT_EQ(test, -ENOMEM, PTR_ERR(clone));

	rust_fdtable_test_fail_next(RUST_FDTABLE_TEST_FAIL_ALLOC);
	KUNIT_EXPECT_EQ(test, -ENOMEM,
			rust_fdtable_test_alloc_range(max_fds + 1,
						      max_fds + 2, 0));
	rust_fdtable_test_fail_next(0);

	KUNIT_EXPECT_EQ(test, failures + 2,
			rust_fdtable_test_failure_count());
}

static int fdtable_test_reader(void *data)
{
	struct fdtable_reader_context *context = data;
	unsigned int count = 0;

	complete(&context->started);
	while (!atomic_read(&context->stop)) {
		struct file *file;

		file = rust_fdtable_test_fget(context->files, context->fd, 0);
		if (file) {
			if (file != context->file)
				atomic_inc(&context->errors);
			fput(file);
			count++;
		}
		if (!(count & 63))
			cond_resched();
	}
	atomic_set(&context->lookups, count);
	complete(&context->done);
	return 0;
}

static void fdtable_lifecycle_test_concurrent_lookup(struct kunit *test)
{
	struct fdtable_reader_context contexts[FDTABLE_TEST_WORKERS] = {};
	struct task_struct *tasks[FDTABLE_TEST_WORKERS] = {};
	struct files_struct *clone;
	struct file *file;
	struct file *removed;
	unsigned int old_max;
	unsigned int index;
	unsigned int fd;
	int error;
	bool timed_out = false;

	atomic_set(&fdtable_release_count, 0);
	file = fdtable_test_create_file();
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(file));
	fdtable_test_install_file(test, file, &fd);

	clone = dup_fd(current->files, NULL);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(clone));
	atomic_inc(&clone->count);

	for (index = 0; index < ARRAY_SIZE(contexts); index++) {
		contexts[index].files = clone;
		contexts[index].file = file;
		contexts[index].fd = fd;
		atomic_set(&contexts[index].stop, 0);
		atomic_set(&contexts[index].lookups, 0);
		atomic_set(&contexts[index].errors, 0);
		init_completion(&contexts[index].started);
		init_completion(&contexts[index].done);
		tasks[index] = kthread_run(fdtable_test_reader,
					   &contexts[index],
					   "fdtable-reader-%u", index);
		KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(tasks[index]));
		get_task_struct(tasks[index]);
	}

	for (index = 0; index < ARRAY_SIZE(contexts); index++) {
		if (!fdtable_test_wait(&contexts[index].started))
			timed_out = true;
	}

	spin_lock(&clone->file_lock);
	old_max = files_fdtable(clone)->max_fds;
	error = rust_fdtable_expand_files(clone,
					  old_max + FDTABLE_TEST_LOOKUPS);
	spin_unlock(&clone->file_lock);
	KUNIT_EXPECT_EQ(test, 0, error);
	spin_lock(&clone->file_lock);
	KUNIT_EXPECT_GT(test, files_fdtable(clone)->max_fds, old_max);
	spin_unlock(&clone->file_lock);

	for (index = 0; index < FDTABLE_TEST_LOOKUPS / 16; index++) {
		struct file *removed;

		spin_lock(&clone->file_lock);
		removed = file_close_fd_locked(clone, fd);
		spin_unlock(&clone->file_lock);
		KUNIT_EXPECT_PTR_EQ(test, file, removed);
		if (!removed)
			break;

		fput(removed);
		get_file(file);
		rust_fdtable_test_install(clone, fd, file);
	}

	for (index = 0; index < ARRAY_SIZE(contexts); index++)
		atomic_set(&contexts[index].stop, 1);
	for (index = 0; index < ARRAY_SIZE(contexts); index++) {
		if (!fdtable_test_wait(&contexts[index].done))
			timed_out = true;
	}
	for (index = 0; index < ARRAY_SIZE(contexts); index++) {
		KUNIT_EXPECT_EQ(test, 0, kthread_stop_put(tasks[index]));
		KUNIT_EXPECT_GT(test, atomic_read(&contexts[index].lookups), 0);
		KUNIT_EXPECT_EQ(test, 0, atomic_read(&contexts[index].errors));
	}

	KUNIT_EXPECT_FALSE(test, timed_out);
	put_files_struct(clone);

	spin_lock(&clone->file_lock);
	removed = file_close_fd_locked(clone, fd);
	spin_unlock(&clone->file_lock);
	KUNIT_EXPECT_PTR_EQ(test, file, removed);
	if (removed)
		fput(removed);
	put_files_struct(clone);

	KUNIT_EXPECT_EQ(test, 0, close_fd(fd));
	task_work_run();
	flush_delayed_fput();
	KUNIT_EXPECT_EQ(test, 1, atomic_read(&fdtable_release_count));
}
#endif

static struct kunit_case fdtable_lifecycle_test_cases[] = {
#ifdef CONFIG_RUST_FDTABLE_LIFECYCLE
	KUNIT_CASE(fdtable_lifecycle_test_fdget_state),
	KUNIT_CASE(fdtable_lifecycle_test_failure_injection),
	KUNIT_CASE(fdtable_lifecycle_test_concurrent_lookup),
#endif
	KUNIT_CASE(fdtable_lifecycle_test_lookup_close),
	KUNIT_CASE(fdtable_lifecycle_test_expand),
	KUNIT_CASE(fdtable_lifecycle_test_dup_release),
	{}
};

static struct kunit_suite fdtable_lifecycle_test_suite = {
	.name = "fdtable_lifecycle",
	.test_cases = fdtable_lifecycle_test_cases,
};

kunit_test_suite(fdtable_lifecycle_test_suite);

MODULE_DESCRIPTION("KUnit tests for file descriptor table lifecycle");
MODULE_LICENSE("GPL");
