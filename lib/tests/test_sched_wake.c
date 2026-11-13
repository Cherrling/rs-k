// SPDX-License-Identifier: GPL-2.0-only

#include <kunit/test.h>

#include <linux/completion.h>
#include <linux/cpu.h>
#include <linux/kthread.h>
#include <linux/preempt.h>
#include <linux/processor.h>
#include <linux/sched.h>
#include <linux/sched-rust.h>
#include <linux/slab.h>

static int sched_wake_test_thread(void *data)
{
	struct completion *started = data;

	complete(started);
	while (!kthread_should_stop())
		schedule_timeout_interruptible(msecs_to_jiffies(10));

	return 0;
}

static int sched_wake_test_runnable_thread(void *data)
{
	struct completion *started = data;

	preempt_disable();
	__set_current_state(TASK_INTERRUPTIBLE);
	complete(started);
	while (!kthread_should_stop())
		cpu_relax();
	__set_current_state(TASK_RUNNING);
	preempt_enable();

	return 0;
}

#ifdef CONFIG_RUST_SCHED_BLOCK_TASK
static int sched_block_test_thread(void *data)
{
	struct completion *finished = data;

	schedule_timeout_uninterruptible(msecs_to_jiffies(1));
	complete(finished);

	return 0;
}
#endif

#ifdef CONFIG_RUST_SCHED_WAKE_STATE
static void sched_wake_test_state_match(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, 1,
			rust_sched_state_match_select(TASK_INTERRUPTIBLE,
						      TASK_RUNNING,
						      TASK_NORMAL));
	KUNIT_EXPECT_EQ(test, -1,
			rust_sched_state_match_select(TASK_RTLOCK_WAIT,
						      TASK_INTERRUPTIBLE,
						      TASK_NORMAL));
	KUNIT_EXPECT_EQ(test, 0,
			rust_sched_state_match_select(TASK_WAKING, TASK_RUNNING,
						      TASK_NORMAL));
	KUNIT_EXPECT_EQ(test, 1,
			rust_sched_state_match_select(TASK_INTERRUPTIBLE,
						      TASK_UNINTERRUPTIBLE,
						      TASK_NORMAL));
}

static void sched_wake_test_saved_state_transition(struct kunit *test)
{
	struct task_struct *task;
	int success;

	task = kzalloc_obj(*task);
	KUNIT_ASSERT_NOT_NULL(test, task);

	task->__state = TASK_RTLOCK_WAIT;
	task->saved_state = TASK_INTERRUPTIBLE;
	success = 0;
	KUNIT_EXPECT_FALSE(test,
			   rust_sched_ttwu_state_match(task, TASK_NORMAL, &success));
	KUNIT_EXPECT_EQ(test, 1, success);
	KUNIT_EXPECT_EQ(test, TASK_RUNNING, task->saved_state);

	task->__state = TASK_UNINTERRUPTIBLE;
	task->saved_state = TASK_RUNNING;
	success = 0;
	KUNIT_EXPECT_TRUE(test,
			  rust_sched_ttwu_state_match(task, TASK_NORMAL, &success));
	KUNIT_EXPECT_EQ(test, 1, success);
	KUNIT_EXPECT_EQ(test, TASK_RUNNING, task->saved_state);

	task->__state = TASK_WAKING;
	success = 1;
	KUNIT_EXPECT_FALSE(test,
			   rust_sched_ttwu_state_match(task, TASK_NORMAL, &success));
	KUNIT_EXPECT_EQ(test, 0, success);

	kfree(task);
}
#endif

#ifdef CONFIG_RUST_SCHED_TTWU_ACTIVATE
static void sched_wake_test_activate_plan(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test,
			RUST_SCHED_TTWU_ACTIVATE_DEC_LOAD |
			RUST_SCHED_TTWU_ACTIVATE_RQ_SELECTED |
			RUST_SCHED_TTWU_ACTIVATE_IO_WAIT,
			rust_sched_ttwu_activate_select(true, true, false, true));
	KUNIT_EXPECT_EQ(test,
			RUST_SCHED_TTWU_ACTIVATE_MIGRATED,
			rust_sched_ttwu_activate_select(false, false, true, true));
	KUNIT_EXPECT_EQ(test, 0,
			rust_sched_ttwu_activate_select(false, false, false, false));
}

static void sched_wake_test_activate_task_flags(struct kunit *test)
{
	struct task_struct *task;

	task = kzalloc_obj(*task);
	KUNIT_ASSERT_NOT_NULL(test, task);

	KUNIT_EXPECT_EQ(test, 0,
			rust_sched_ttwu_task_activate_flags_select(task));
	task->sched_contributes_to_load = 1;
	KUNIT_EXPECT_EQ(test, RUST_SCHED_TTWU_ACTIVATE_DEC_LOAD,
			rust_sched_ttwu_task_activate_flags_select(task));
	task->sched_contributes_to_load = 0;
	task->in_iowait = 1;
	KUNIT_EXPECT_EQ(test, RUST_SCHED_TTWU_ACTIVATE_IO_WAIT,
			rust_sched_ttwu_task_activate_flags_select(task));
	task->sched_contributes_to_load = 1;
	KUNIT_EXPECT_EQ(test,
			RUST_SCHED_TTWU_ACTIVATE_DEC_LOAD |
			RUST_SCHED_TTWU_ACTIVATE_IO_WAIT,
			rust_sched_ttwu_task_activate_flags_select(task));

	kfree(task);
}

static void sched_wake_test_wake_flags(struct kunit *test)
{
	unsigned int migrated = RUST_SCHED_WF_MIGRATED;
	unsigned int rq_selected = RUST_SCHED_WF_RQ_SELECTED;
	unsigned int both = rq_selected | migrated;

	KUNIT_EXPECT_EQ(test, 0, rust_sched_ttwu_wake_flags_select(0));
	KUNIT_EXPECT_EQ(test, RUST_SCHED_TTWU_ACTIVATE_RQ_SELECTED,
			rust_sched_ttwu_wake_flags_select(rq_selected));
	KUNIT_EXPECT_EQ(test, RUST_SCHED_TTWU_ACTIVATE_MIGRATED,
			rust_sched_ttwu_wake_flags_select(migrated));
	KUNIT_EXPECT_EQ(test,
			RUST_SCHED_TTWU_ACTIVATE_RQ_SELECTED |
			RUST_SCHED_TTWU_ACTIVATE_MIGRATED,
			rust_sched_ttwu_wake_flags_select(both));
}

static void sched_wake_test_activate_enqueue_flags(struct kunit *test)
{
	int base = RUST_SCHED_ENQUEUE_WAKEUP |
		   RUST_SCHED_ENQUEUE_NOCLOCK;
	int rq_selected = base | RUST_SCHED_ENQUEUE_RQ_SELECTED;
	int migrated = base | RUST_SCHED_ENQUEUE_MIGRATED;
	int both = rq_selected | RUST_SCHED_ENQUEUE_MIGRATED;

	KUNIT_EXPECT_EQ(test, base,
			rust_sched_ttwu_activate_flags_select(false, false));
	KUNIT_EXPECT_EQ(test, rq_selected,
			rust_sched_ttwu_activate_flags_select(true, false));
	KUNIT_EXPECT_EQ(test, migrated,
			rust_sched_ttwu_activate_flags_select(false, true));
	KUNIT_EXPECT_EQ(test, both,
			rust_sched_ttwu_activate_flags_select(true, true));
}

static void sched_wake_test_dec_uninterruptible(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, 1,
			rust_sched_ttwu_dec_uninterruptible_select(2));
}

static void sched_wake_test_dec_iowait(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, 1, rust_sched_ttwu_dec_iowait_select(2));
}

static void sched_wake_test_realtime_task_woken(struct kunit *test)
{
	struct task_struct *task;
	struct completion started;

	init_completion(&started);
	task = kthread_create(sched_wake_test_thread, &started,
			      "sched-wake-rt-test");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, task);

	sched_set_fifo(task);
	KUNIT_EXPECT_EQ(test, 1, wake_up_process(task));
	wait_for_completion(&started);
	KUNIT_EXPECT_EQ(test, 0, kthread_stop(task));
}
#endif

#ifdef CONFIG_RUST_SCHED_BLOCK_TASK
static void sched_wake_test_block_plan(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, RUST_SCHED_BLOCK_CONTRIBUTES_LOAD,
			rust_sched_block_task_select(TASK_UNINTERRUPTIBLE));
	KUNIT_EXPECT_EQ(test, RUST_SCHED_BLOCK_CONTRIBUTES_LOAD,
			rust_sched_block_task_select(TASK_UNINTERRUPTIBLE |
						     TASK_FREEZABLE));
	KUNIT_EXPECT_EQ(test, 0,
			rust_sched_block_task_select(TASK_UNINTERRUPTIBLE |
						     TASK_NOLOAD));
	KUNIT_EXPECT_EQ(test, RUST_SCHED_BLOCK_SPECIAL,
			rust_sched_block_task_select(TASK_UNINTERRUPTIBLE |
						     TASK_FROZEN));
	KUNIT_EXPECT_EQ(test, 0,
			rust_sched_block_task_select(TASK_INTERRUPTIBLE));
	KUNIT_EXPECT_EQ(test, RUST_SCHED_BLOCK_SPECIAL,
			rust_sched_block_task_select(TASK_WAKING));
	KUNIT_EXPECT_EQ(test, RUST_SCHED_BLOCK_SPECIAL,
			rust_sched_block_task_select(TASK_PARKED));
}

static void sched_wake_test_block_task_flags(struct kunit *test)
{
	struct task_struct *task;

	task = kzalloc_obj(*task);
	KUNIT_ASSERT_NOT_NULL(test, task);

	rust_sched_block_task_set_contributes_load_select(task, true);
	KUNIT_EXPECT_TRUE(test, task->sched_contributes_to_load);
	rust_sched_block_task_set_contributes_load_select(task, false);
	KUNIT_EXPECT_FALSE(test, task->sched_contributes_to_load);

	kfree(task);
}

static void sched_wake_test_uninterruptible_timeout(struct kunit *test)
{
	struct task_struct *task;
	struct completion finished;

	init_completion(&finished);
	task = kthread_create(sched_block_test_thread, &finished,
			      "sched-block-test");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, task);

	KUNIT_EXPECT_EQ(test, 1, wake_up_process(task));
	wait_for_completion(&finished);
	KUNIT_EXPECT_EQ(test, 0, kthread_stop(task));
}
#endif

#ifdef CONFIG_RUST_SCHED_TTWU_RUNNABLE
static void sched_wake_test_runnable_task_flags(struct kunit *test)
{
	struct task_struct *task;

	task = kzalloc_obj(*task);
	KUNIT_ASSERT_NOT_NULL(test, task);

	KUNIT_EXPECT_EQ(test, 0, rust_sched_ttwu_task_flags_select(task));
	task->is_blocked = 1;
	KUNIT_EXPECT_EQ(test, RUST_SCHED_TTWU_TASK_IS_BLOCKED,
			rust_sched_ttwu_task_flags_select(task));
	task->is_blocked = 0;
	task->on_cpu = 1;
	KUNIT_EXPECT_EQ(test, RUST_SCHED_TTWU_TASK_ON_CPU,
			rust_sched_ttwu_task_flags_select(task));
	task->on_cpu = 0;
	task->se.sched_delayed = 1;
	KUNIT_EXPECT_EQ(test, RUST_SCHED_TTWU_TASK_DELAYED,
			rust_sched_ttwu_task_flags_select(task));
	task->se.sched_delayed = 0;
	task->is_blocked = 1;
	task->on_cpu = 1;
	KUNIT_EXPECT_EQ(test, RUST_SCHED_TTWU_TASK_IS_BLOCKED |
			      RUST_SCHED_TTWU_TASK_ON_CPU,
			rust_sched_ttwu_task_flags_select(task));
	task->se.sched_delayed = 1;
	KUNIT_EXPECT_EQ(test, RUST_SCHED_TTWU_TASK_IS_BLOCKED |
			      RUST_SCHED_TTWU_TASK_ON_CPU |
			      RUST_SCHED_TTWU_TASK_DELAYED,
			rust_sched_ttwu_task_flags_select(task));

	kfree(task);
}
#endif

#ifdef CONFIG_RUST_SCHED_RQ_TRANSITION
static void sched_wake_test_rq_transition_plan(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, RUST_SCHED_RQ_ADD_MIGRATED,
			rust_sched_rq_transition_select(true, 0));
	KUNIT_EXPECT_EQ(test, RUST_SCHED_RQ_WARN_DEQUEUE_SLEEP,
			rust_sched_rq_transition_select(false,
							RUST_SCHED_DEQUEUE_SLEEP));
	KUNIT_EXPECT_EQ(test, 0,
			rust_sched_rq_transition_select(false, 0));
}
#endif

static void sched_wake_test_current(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, 0, wake_up_process(current));
	KUNIT_EXPECT_EQ(test, TASK_RUNNING, current->__state);
}

static void sched_wake_test_stopped_task(struct kunit *test)
{
	struct task_struct *task;
	struct completion started;

	init_completion(&started);
	task = kthread_create(sched_wake_test_thread, &started,
			      "sched-wake-test");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, task);

	KUNIT_EXPECT_EQ(test, 1, wake_up_process(task));
	wait_for_completion(&started);
	KUNIT_EXPECT_EQ(test, 0, kthread_stop(task));
}

static void sched_wake_test_runnable_task(struct kunit *test)
{
	struct task_struct *task;
	struct completion started;

	if (num_online_cpus() < 2)
		kunit_skip(test, "runnable wake test needs two online CPUs");

	init_completion(&started);
	task = kthread_create(sched_wake_test_runnable_thread, &started,
			      "sched-runnable-test");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, task);

	KUNIT_EXPECT_EQ(test, 1, wake_up_process(task));
	wait_for_completion(&started);

	KUNIT_EXPECT_EQ(test, 1, wake_up_process(task));
	KUNIT_EXPECT_EQ(test, TASK_RUNNING, READ_ONCE(task->__state));
	KUNIT_EXPECT_EQ(test, 0, kthread_stop(task));
}

static struct kunit_case sched_wake_test_cases[] = {
#ifdef CONFIG_RUST_SCHED_WAKE_STATE
	KUNIT_CASE(sched_wake_test_state_match),
	KUNIT_CASE(sched_wake_test_saved_state_transition),
#endif
#ifdef CONFIG_RUST_SCHED_TTWU_ACTIVATE
	KUNIT_CASE(sched_wake_test_activate_plan),
	KUNIT_CASE(sched_wake_test_activate_task_flags),
	KUNIT_CASE(sched_wake_test_wake_flags),
	KUNIT_CASE(sched_wake_test_activate_enqueue_flags),
	KUNIT_CASE(sched_wake_test_dec_uninterruptible),
	KUNIT_CASE(sched_wake_test_dec_iowait),
	KUNIT_CASE(sched_wake_test_realtime_task_woken),
#endif
#ifdef CONFIG_RUST_SCHED_BLOCK_TASK
	KUNIT_CASE(sched_wake_test_block_plan),
	KUNIT_CASE(sched_wake_test_block_task_flags),
	KUNIT_CASE(sched_wake_test_uninterruptible_timeout),
#endif
#ifdef CONFIG_RUST_SCHED_TTWU_RUNNABLE
	KUNIT_CASE(sched_wake_test_runnable_task_flags),
#endif
#ifdef CONFIG_RUST_SCHED_RQ_TRANSITION
	KUNIT_CASE(sched_wake_test_rq_transition_plan),
#endif
	KUNIT_CASE(sched_wake_test_current),
	KUNIT_CASE(sched_wake_test_stopped_task),
	KUNIT_CASE(sched_wake_test_runnable_task),
	{}
};

static struct kunit_suite sched_wake_test_suite = {
	.name = "sched_wake",
	.test_cases = sched_wake_test_cases,
};

kunit_test_suite(sched_wake_test_suite);
