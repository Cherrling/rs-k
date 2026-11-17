// SPDX-License-Identifier: GPL-2.0-only

#include <kunit/test.h>

#include <linux/atomic.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>
#include <linux/rcuwait.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/string.h>

#define RCUWAIT_TEST_TIMEOUT_MS 10000
#define RCUWAIT_TEST_HANDOFFS 32

struct rcuwait_test_context {
	struct rcuwait wait;
	atomic_t started;
	atomic_t ready;
	atomic_t release;
	atomic_t progress;
	atomic_t done;
	atomic_t ret;
	atomic_t errors;
	struct task_struct *task;
};

struct rcuwait_test_timeout_condition {
	unsigned int calls;
	unsigned int success_call;
	bool result;
	bool first_active;
	bool first_state_set;
	bool second_active;
	bool second_state_set;
};

#define RCUWAIT_TEST_COND(wait, condition)				\
	rcuwait_test_timeout_initial_condition((wait), (condition))

static bool rcuwait_test_wait_for(atomic_t *value, int target)
{
	unsigned long deadline = jiffies +
				 msecs_to_jiffies(RCUWAIT_TEST_TIMEOUT_MS);

	while (atomic_read(value) < target && time_before(jiffies, deadline))
		cond_resched();

	return atomic_read(value) >= target;
}

static bool rcuwait_test_wait_sleeping(struct rcuwait_test_context *context)
{
	unsigned long deadline = jiffies +
				 msecs_to_jiffies(RCUWAIT_TEST_TIMEOUT_MS);

	while (time_before(jiffies, deadline)) {
		if (rcuwait_active(&context->wait) && context->task &&
		    READ_ONCE(context->task->__state) != TASK_RUNNING)
			return true;
		cond_resched();
	}

	return false;
}

static int rcuwait_test_waiter(void *data)
{
	struct rcuwait_test_context *context = data;

	context->task = current;
	atomic_set(&context->started, 1);
	atomic_set(&context->ret,
		   rcuwait_wait_event(&context->wait,
				      atomic_read(&context->release) != 0,
				      TASK_UNINTERRUPTIBLE));
	atomic_set(&context->done, 1);

	return 0;
}

static int rcuwait_test_timeout_waiter(void *data)
{
	struct rcuwait_test_context *context = data;

	context->task = current;
	atomic_set(&context->started, 1);
	atomic_set(&context->ret,
		   rcuwait_wait_event_timeout(&context->wait,
					      atomic_read(&context->release) != 0,
					      TASK_UNINTERRUPTIBLE,
					      msecs_to_jiffies(1000)));
	atomic_set(&context->done, 1);

	return 0;
}

static int rcuwait_test_signal_waiter(void *data)
{
	struct rcuwait_test_context *context = data;

	allow_kernel_signal(SIGUSR1);
	context->task = current;
	atomic_set(&context->started, 1);
	atomic_set(&context->ret,
		   rcuwait_wait_event(&context->wait, false,
				      TASK_INTERRUPTIBLE));
	flush_signals(current);
	disallow_signal(SIGUSR1);
	atomic_set(&context->done, 1);

	return 0;
}

static int rcuwait_test_handoff_waiter(void *data)
{
	struct rcuwait_test_context *context = data;
	int target;

	context->task = current;
	atomic_set(&context->started, 1);
	for (target = 1; target <= RCUWAIT_TEST_HANDOFFS; target++) {
		atomic_set(&context->ready, target);
		if (rcuwait_wait_event(&context->wait,
				       rcuwait_active(&context->wait) &&
				       (atomic_read(&context->release) >= target ||
					kthread_should_stop()),
				       TASK_INTERRUPTIBLE)) {
			if (!kthread_should_stop())
				atomic_inc(&context->errors);
		}
		if (kthread_should_stop())
			break;
		atomic_set(&context->progress, target);
	}
	atomic_set(&context->done, 1);

	return 0;
}

static bool rcuwait_test_start(struct kunit *test,
			       struct rcuwait_test_context *context,
			       int (*threadfn)(void *),
			       struct task_struct **task, const char *name)
{
	*task = kthread_run(threadfn, context, "%s", name);
	if (IS_ERR_OR_NULL(*task)) {
		*task = NULL;
		KUNIT_FAIL(test, "failed to start rcuwait worker");
		return false;
	}

	if (!rcuwait_test_wait_for(&context->started, 1)) {
		KUNIT_FAIL(test, "rcuwait worker did not start");
		kthread_stop(*task);
		*task = NULL;
		return false;
	}

	return true;
}

static void rcuwait_test_lifecycle(struct kunit *test)
{
	struct rcuwait wait;
	bool running;

	rcuwait_init(&wait);
	KUNIT_EXPECT_FALSE(test, rcuwait_active(&wait));
	KUNIT_EXPECT_EQ(test, rcuwait_wake_up(&wait), 0);

	prepare_to_rcuwait(&wait);
	KUNIT_EXPECT_TRUE(test, rcuwait_active(&wait));
	KUNIT_EXPECT_PTR_EQ(test, rcu_access_pointer(wait.task), current);
	finish_rcuwait(&wait);

	running = READ_ONCE(current->__state) == TASK_RUNNING;
	KUNIT_EXPECT_FALSE(test, rcuwait_active(&wait));
	KUNIT_EXPECT_TRUE(test, running);
}

static bool rcuwait_test_immediate_condition(struct rcuwait *wait,
					     bool *active, bool *state_set)
{
	*active = rcuwait_active(wait);
	*state_set = READ_ONCE(current->__state) != TASK_RUNNING;

	return true;
}

static void rcuwait_test_condition_order(struct kunit *test)
{
	struct rcuwait wait;
	bool active = false;
	bool state_set = false;
	long ret;

	rcuwait_init(&wait);
	ret = rcuwait_wait_event(&wait,
				 rcuwait_test_immediate_condition(&wait, &active,
								  &state_set),
				 TASK_INTERRUPTIBLE);

	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_TRUE(test, active);
	KUNIT_EXPECT_TRUE(test, state_set);
	KUNIT_EXPECT_FALSE(test, rcuwait_active(&wait));
	KUNIT_EXPECT_EQ(test, READ_ONCE(current->__state), TASK_RUNNING);
}

static void rcuwait_test_wake(struct kunit *test)
{
	struct rcuwait_test_context context = {};
	struct task_struct *task = NULL;
	bool sleeping;

	rcuwait_init(&context.wait);
	if (!rcuwait_test_start(test, &context, rcuwait_test_waiter, &task,
				"rcuwait-wake"))
		return;

	sleeping = rcuwait_test_wait_sleeping(&context);
	if (!sleeping) {
		atomic_set(&context.release, 1);
		rcuwait_wake_up(&context.wait);
		rcuwait_test_wait_for(&context.done, 1);
		kthread_stop(task);
		KUNIT_FAIL(test, "waiter did not sleep");
		return;
	}
	atomic_set(&context.release, 1);
	rcuwait_wake_up(&context.wait);
	if (!rcuwait_test_wait_for(&context.done, 1)) {
		kthread_stop(task);
		KUNIT_FAIL(test, "waiter did not resume");
		return;
	}
	KUNIT_EXPECT_EQ(test, kthread_stop(task), 0);

	KUNIT_EXPECT_EQ(test, atomic_read(&context.ret), 0);
	KUNIT_EXPECT_FALSE(test, rcuwait_active(&context.wait));
}

static void rcuwait_test_timeout(struct kunit *test)
{
	struct rcuwait wait;
	long timeout;
	long ret;

	rcuwait_init(&wait);
	timeout = max_t(long, 1, msecs_to_jiffies(10));
	ret = rcuwait_wait_event_timeout(&wait, false,
					 TASK_UNINTERRUPTIBLE, timeout);

	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_FALSE(test, rcuwait_active(&wait));
	KUNIT_EXPECT_EQ(test, READ_ONCE(current->__state), TASK_RUNNING);

	ret = rcuwait_wait_event_timeout(&wait, true,
					 TASK_UNINTERRUPTIBLE, 7);
	KUNIT_EXPECT_EQ(test, ret, 7);
	KUNIT_EXPECT_FALSE(test, rcuwait_active(&wait));
}

static bool
rcuwait_test_timeout_initial_condition(struct rcuwait *wait,
				       struct rcuwait_test_timeout_condition *condition)
{
	condition->calls++;
	if (condition->calls == 1) {
		condition->first_active = rcuwait_active(wait);
		condition->first_state_set =
			READ_ONCE(current->__state) != TASK_RUNNING;
	} else if (condition->calls == 2) {
		condition->second_active = rcuwait_active(wait);
		condition->second_state_set =
			READ_ONCE(current->__state) != TASK_RUNNING;
	}

	return condition->result && condition->calls == condition->success_call;
}

static void rcuwait_test_timeout_initial(struct kunit *test)
{
	struct rcuwait_test_timeout_condition condition = {};
	struct rcuwait wait;
	long ret;

	rcuwait_init(&wait);
	ret =
		rcuwait_wait_event_timeout(&wait,
					   RCUWAIT_TEST_COND(&wait, &condition),
					   TASK_INTERRUPTIBLE, 0);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, condition.calls, 1);
	KUNIT_EXPECT_FALSE(test, condition.first_active);
	KUNIT_EXPECT_FALSE(test, condition.first_state_set);
	KUNIT_EXPECT_FALSE(test, rcuwait_active(&wait));
	KUNIT_EXPECT_EQ(test, READ_ONCE(current->__state), TASK_RUNNING);

	memset(&condition, 0, sizeof(condition));
	condition.result = true;
	condition.success_call = 1;
	ret =
		rcuwait_wait_event_timeout(&wait,
					   RCUWAIT_TEST_COND(&wait, &condition),
					   TASK_INTERRUPTIBLE, 0);
	KUNIT_EXPECT_EQ(test, ret, 1);
	KUNIT_EXPECT_EQ(test, condition.calls, 1);
	KUNIT_EXPECT_FALSE(test, condition.first_active);
	KUNIT_EXPECT_FALSE(test, condition.first_state_set);
	KUNIT_EXPECT_FALSE(test, rcuwait_active(&wait));
	KUNIT_EXPECT_EQ(test, READ_ONCE(current->__state), TASK_RUNNING);

	memset(&condition, 0, sizeof(condition));
	condition.result = true;
	condition.success_call = 2;
	ret =
		rcuwait_wait_event_timeout(&wait,
					   RCUWAIT_TEST_COND(&wait, &condition),
					   TASK_INTERRUPTIBLE, 7);
	KUNIT_EXPECT_EQ(test, ret, 7);
	KUNIT_EXPECT_EQ(test, condition.calls, 2);
	KUNIT_EXPECT_FALSE(test, condition.first_active);
	KUNIT_EXPECT_FALSE(test, condition.first_state_set);
	KUNIT_EXPECT_TRUE(test, condition.second_active);
	KUNIT_EXPECT_TRUE(test, condition.second_state_set);
	KUNIT_EXPECT_FALSE(test, rcuwait_active(&wait));
	KUNIT_EXPECT_EQ(test, READ_ONCE(current->__state), TASK_RUNNING);
}

static void rcuwait_test_timeout_wake(struct kunit *test)
{
	struct rcuwait_test_context context = {};
	struct task_struct *task = NULL;
	bool sleeping;
	long ret;

	rcuwait_init(&context.wait);
	if (!rcuwait_test_start(test, &context, rcuwait_test_timeout_waiter,
				&task, "rcuwait-timeout"))
		return;

	sleeping = rcuwait_test_wait_sleeping(&context);
	if (!sleeping) {
		atomic_set(&context.release, 1);
		rcuwait_wake_up(&context.wait);
		rcuwait_test_wait_for(&context.done, 1);
		kthread_stop(task);
		KUNIT_FAIL(test, "timeout waiter did not sleep");
		return;
	}
	atomic_set(&context.release, 1);
	rcuwait_wake_up(&context.wait);
	if (!rcuwait_test_wait_for(&context.done, 1)) {
		kthread_stop(task);
		KUNIT_FAIL(test, "timeout waiter did not resume");
		return;
	}
	KUNIT_EXPECT_EQ(test, kthread_stop(task), 0);

	ret = atomic_read(&context.ret);
	KUNIT_EXPECT_GT(test, ret, 0);
	KUNIT_EXPECT_FALSE(test, rcuwait_active(&context.wait));
}

static void rcuwait_test_signal(struct kunit *test)
{
	struct rcuwait_test_context context = {};
	struct task_struct *task = NULL;
	bool sleeping;

	rcuwait_init(&context.wait);
	if (!rcuwait_test_start(test, &context, rcuwait_test_signal_waiter,
				&task, "rcuwait-signal"))
		return;

	sleeping = rcuwait_test_wait_sleeping(&context);
	if (!sleeping) {
		send_sig(SIGUSR1, context.task, 1);
		rcuwait_wake_up(&context.wait);
		rcuwait_test_wait_for(&context.done, 1);
		kthread_stop(task);
		KUNIT_FAIL(test, "signal waiter did not sleep");
		return;
	}
	KUNIT_EXPECT_EQ(test, send_sig(SIGUSR1, context.task, 1), 0);
	rcuwait_wake_up(&context.wait);
	if (!rcuwait_test_wait_for(&context.done, 1)) {
		kthread_stop(task);
		KUNIT_FAIL(test, "signal waiter did not resume");
		return;
	}
	KUNIT_EXPECT_EQ(test, kthread_stop(task), 0);

	KUNIT_EXPECT_EQ(test, atomic_read(&context.ret), -EINTR);
	KUNIT_EXPECT_FALSE(test, rcuwait_active(&context.wait));
}

static void rcuwait_test_handoffs(struct kunit *test)
{
	struct rcuwait_test_context context = {};
	struct task_struct *task = NULL;
	int target;

	rcuwait_init(&context.wait);
	if (!rcuwait_test_start(test, &context, rcuwait_test_handoff_waiter,
				&task, "rcuwait-handoff"))
		return;

	for (target = 1; target <= RCUWAIT_TEST_HANDOFFS; target++) {
		if (!rcuwait_test_wait_for(&context.ready, target)) {
			KUNIT_FAIL(test, "worker did not publish handoff");
			break;
		}
		if (!rcuwait_test_wait_sleeping(&context)) {
			KUNIT_FAIL(test, "worker did not enter handoff wait");
			break;
		}
		atomic_set(&context.release, target);
		rcuwait_wake_up(&context.wait);
		if (!rcuwait_test_wait_for(&context.progress, target)) {
			KUNIT_FAIL(test, "worker did not complete handoff");
			break;
		}
	}

	atomic_set(&context.release, RCUWAIT_TEST_HANDOFFS + 1);
	rcuwait_wake_up(&context.wait);
	KUNIT_EXPECT_EQ(test, kthread_stop(task), 0);
	KUNIT_EXPECT_EQ(test, atomic_read(&context.errors), 0);
	KUNIT_EXPECT_EQ(test, atomic_read(&context.progress),
			RCUWAIT_TEST_HANDOFFS);
	KUNIT_EXPECT_FALSE(test, rcuwait_active(&context.wait));
}

static struct kunit_case rcuwait_test_cases[] = {
	KUNIT_CASE(rcuwait_test_lifecycle),
	KUNIT_CASE(rcuwait_test_condition_order),
	KUNIT_CASE(rcuwait_test_wake),
	KUNIT_CASE(rcuwait_test_timeout),
	KUNIT_CASE(rcuwait_test_timeout_initial),
	KUNIT_CASE(rcuwait_test_timeout_wake),
	KUNIT_CASE(rcuwait_test_signal),
	KUNIT_CASE(rcuwait_test_handoffs),
	{}
};

static struct kunit_suite rcuwait_test_suite = {
	.name = "rcuwait",
	.test_cases = rcuwait_test_cases,
};

kunit_test_suites(&rcuwait_test_suite);

MODULE_DESCRIPTION("rcuwait KUnit test suite");
MODULE_LICENSE("GPL");
