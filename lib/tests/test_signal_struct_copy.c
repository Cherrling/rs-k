// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>
#include <linux/completion.h>
#include <linux/hrtimer.h>
#include <linux/kthread.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/signal.h>
#include <linux/signal-struct-copy-rust.h>
#include <linux/wait.h>

#define SIGNAL_STRUCT_COPY_TEST_TIMEOUT (5 * HZ)

struct signal_struct_copy_single_context {
	struct completion started;
	struct rlimit cpu_rlimit;
	short oom_score_adj;
	short oom_score_adj_min;
	bool valid;
};

static bool
signal_struct_copy_state_valid(const struct signal_struct_copy_single_context *context)
{
	struct signal_struct *signal = current->signal;

	if (!signal)
		return false;
	if (signal->nr_threads != 1 || signal->quick_threads != 1)
		return false;
	if (atomic_read(&signal->live) != 1)
		return false;
	if (refcount_read(&signal->sigcnt) != 1)
		return false;
	if (signal->thread_head.next != &current->thread_node ||
	    signal->thread_head.prev != &current->thread_node)
		return false;
	if (current->thread_node.next != &signal->thread_head ||
	    current->thread_node.prev != &signal->thread_head)
		return false;
	if (!list_empty(&signal->wait_chldexit.head))
		return false;
	if (signal->curr_target != current)
		return false;
	if (!sigisemptyset(&signal->shared_pending.signal))
		return false;
	if (signal->multiprocess.first)
		return false;
	if (signal->rlim[RLIMIT_CPU].rlim_cur != context->cpu_rlimit.rlim_cur ||
	    signal->rlim[RLIMIT_CPU].rlim_max != context->cpu_rlimit.rlim_max)
		return false;
	if (signal->oom_score_adj != context->oom_score_adj ||
	    signal->oom_score_adj_min != context->oom_score_adj_min)
		return false;
#ifdef CONFIG_POSIX_TIMERS
	if (!hlist_empty(&signal->posix_timers) ||
	    !hlist_empty(&signal->ignored_posix_timers))
		return false;
	if (hrtimer_active(&signal->real_timer))
		return false;
#endif
	return true;
}

static int signal_struct_copy_single_thread(void *data)
{
	struct signal_struct_copy_single_context *context = data;

	context->valid = signal_struct_copy_state_valid(context);
	complete(&context->started);

	while (!kthread_should_stop())
		schedule();

	return 0;
}

static void signal_struct_copy_test_single_thread(struct kunit *test)
{
	struct signal_struct_copy_single_context context;
	struct task_struct *task;
#ifdef CONFIG_RUST_SIGNAL_STRUCT_COPY
	u32 complete_count;
	u32 failure_count;
#endif

	memset(&context, 0, sizeof(context));
	init_completion(&context.started);

	task_lock(current->group_leader);
	context.cpu_rlimit = current->signal->rlim[RLIMIT_CPU];
	context.oom_score_adj = current->signal->oom_score_adj;
	context.oom_score_adj_min = current->signal->oom_score_adj_min;
	task_unlock(current->group_leader);

#ifdef CONFIG_RUST_SIGNAL_STRUCT_COPY
	complete_count = rust_signal_struct_copy_test_complete_count();
	failure_count = rust_signal_struct_copy_test_alloc_failure_count();
#endif

	task = kthread_run(signal_struct_copy_single_thread, &context,
			   "signal-struct-copy-single");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, task);
	KUNIT_ASSERT_NE(test, 0,
			wait_for_completion_timeout(&context.started,
						    SIGNAL_STRUCT_COPY_TEST_TIMEOUT));
	KUNIT_EXPECT_TRUE(test, context.valid);

	get_task_struct(task);
	KUNIT_EXPECT_EQ(test, 0, kthread_stop_put(task));
	rcu_barrier();

#ifdef CONFIG_RUST_SIGNAL_STRUCT_COPY
	KUNIT_EXPECT_GT(test, rust_signal_struct_copy_test_complete_count(),
			complete_count);
	KUNIT_EXPECT_EQ(test,
			rust_signal_struct_copy_test_alloc_failure_count(),
			failure_count);
#endif
}

static void signal_struct_copy_test_shared_entry(struct kunit *test)
{
#ifdef CONFIG_RUST_SIGNAL_STRUCT_COPY
	u32 thread_return_count =
		rust_signal_struct_copy_test_thread_return_count();
#endif

	KUNIT_EXPECT_TRUE(test, signal_struct_copy_test_shared());

#ifdef CONFIG_RUST_SIGNAL_STRUCT_COPY
	KUNIT_EXPECT_GT(test,
			rust_signal_struct_copy_test_thread_return_count(),
			thread_return_count);
#endif
}

#ifdef CONFIG_RUST_SIGNAL_STRUCT_COPY
static void signal_struct_copy_test_policy(struct kunit *test)
{
	KUNIT_EXPECT_TRUE(test,
			  rust_signal_struct_copy_test_should_copy(SIGCHLD));
	KUNIT_EXPECT_FALSE(test,
			   rust_signal_struct_copy_test_should_copy(CLONE_THREAD));
	KUNIT_EXPECT_FALSE(test,
			   rust_signal_struct_copy_test_should_copy(CLONE_THREAD |
								    SIGCHLD));
}
#endif

static struct kunit_case signal_struct_copy_test_cases[] = {
	KUNIT_CASE(signal_struct_copy_test_single_thread),
	KUNIT_CASE(signal_struct_copy_test_shared_entry),
#ifdef CONFIG_RUST_SIGNAL_STRUCT_COPY
	KUNIT_CASE(signal_struct_copy_test_policy),
#endif
	{}
};

static struct kunit_suite signal_struct_copy_test_suite = {
	.name = "signal_struct_copy",
	.test_cases = signal_struct_copy_test_cases,
};

kunit_test_suite(signal_struct_copy_test_suite);

MODULE_DESCRIPTION("KUnit tests for thread-group signal structure creation");
MODULE_LICENSE("GPL");
