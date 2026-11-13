// SPDX-License-Identifier: GPL-2.0-only

#include <kunit/test.h>

#include <linux/completion.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/kthread.h>
#include <linux/preempt.h>
#include <linux/processor.h>
#include <linux/sched.h>
#include <linux/sched/idle.h>
#include <linux/sched-rust.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

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

#ifdef CONFIG_RUST_SCHED_WAKE_PREEMPT
static void sched_wake_test_wakeup_preempt_plan(struct kunit *test)
{
	unsigned int (*select)(unsigned int relation, bool donor_queued,
			       bool need_resched);
	unsigned int same = RUST_SCHED_WAKE_PREEMPT_CLASS_SAME;
	unsigned int above = RUST_SCHED_WAKE_PREEMPT_CLASS_ABOVE;
	unsigned int below = RUST_SCHED_WAKE_PREEMPT_CLASS_BELOW;
	unsigned int call = RUST_SCHED_WAKE_PREEMPT_CALL;
	unsigned int skip = RUST_SCHED_WAKE_PREEMPT_SKIP_CLOCK;
	unsigned int higher = call |
			      RUST_SCHED_WAKE_PREEMPT_RESCHED |
			      RUST_SCHED_WAKE_PREEMPT_UPDATE_NEXT;

	select = rust_sched_wakeup_preempt_select;
	KUNIT_EXPECT_EQ(test, call, select(same, false, false));
	KUNIT_EXPECT_EQ(test, call | skip, select(same, true, true));
	KUNIT_EXPECT_EQ(test, higher, select(above, false, false));
	KUNIT_EXPECT_EQ(test, skip, select(below, true, true));
	KUNIT_EXPECT_EQ(test, 0, select(below, false, true));
}
#endif

#ifdef CONFIG_RUST_SCHED_RESCHED_CURR
static void sched_wake_test_resched_tif(struct kunit *test)
{
	int lazy = TIF_NEED_RESCHED_LAZY;
	int urgent = TIF_NEED_RESCHED;

	KUNIT_EXPECT_EQ(test, urgent, rust_sched_resched_tif_select(true, lazy));
	KUNIT_EXPECT_EQ(test, lazy, rust_sched_resched_tif_select(false, lazy));
	KUNIT_EXPECT_EQ(test, urgent, rust_sched_resched_tif_select(false, urgent));
}

static void sched_wake_test_resched_plan(struct kunit *test)
{
	unsigned long urgent_set = 1UL << TIF_NEED_RESCHED;
	unsigned long lazy_set = 1UL << TIF_NEED_RESCHED_LAZY;
	unsigned int local_set = RUST_SCHED_RESCHED_LOCAL_SET;
	unsigned int local_preempt =
		RUST_SCHED_RESCHED_LOCAL_SET |
		RUST_SCHED_RESCHED_LOCAL_PREEMPT;
	unsigned int urgent = TIF_NEED_RESCHED;
	unsigned int lazy = TIF_NEED_RESCHED_LAZY;

	KUNIT_EXPECT_EQ(test, local_preempt,
			rust_sched_resched_plan_select(urgent, 0, true));
	KUNIT_EXPECT_EQ(test, local_set,
			rust_sched_resched_plan_select(lazy, 0, true));
	KUNIT_EXPECT_EQ(test, 0,
			rust_sched_resched_plan_select(urgent, urgent_set, true));
	KUNIT_EXPECT_EQ(test, 0,
			rust_sched_resched_plan_select(lazy, urgent_set, true));
	KUNIT_EXPECT_EQ(test, 0,
			rust_sched_resched_plan_select(lazy, lazy_set, true));
	KUNIT_EXPECT_EQ(test, RUST_SCHED_RESCHED_REMOTE_SET,
			rust_sched_resched_plan_select(urgent, 0, false));
}

static void sched_wake_test_resched_remote(struct kunit *test)
{
	unsigned int urgent = TIF_NEED_RESCHED;
	unsigned int lazy = TIF_NEED_RESCHED_LAZY;

	KUNIT_EXPECT_EQ(test, RUST_SCHED_RESCHED_SEND_IPI,
			rust_sched_resched_remote_select(true, urgent));
	KUNIT_EXPECT_EQ(test, 0,
			rust_sched_resched_remote_select(true, lazy));
	KUNIT_EXPECT_EQ(test, RUST_SCHED_RESCHED_WAKE_IDLE,
			rust_sched_resched_remote_select(false, urgent));
}
#endif

#ifdef CONFIG_RUST_SCHED_RESCHED_CPU
static void sched_wake_test_resched_cpu(struct kunit *test)
{
	KUNIT_EXPECT_TRUE(test, rust_sched_resched_cpu_select(true, false));
	KUNIT_EXPECT_TRUE(test, rust_sched_resched_cpu_select(true, true));
	KUNIT_EXPECT_TRUE(test, rust_sched_resched_cpu_select(false, true));
	KUNIT_EXPECT_FALSE(test, rust_sched_resched_cpu_select(false, false));
}
#endif

#ifdef CONFIG_RUST_SCHED_WAKE_IF_IDLE
static void sched_wake_test_wake_if_idle_plan(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, 0, rust_sched_wake_if_idle_select(false, false));
	KUNIT_EXPECT_EQ(test, 0, rust_sched_wake_if_idle_select(false, true));
	KUNIT_EXPECT_EQ(test, RUST_SCHED_WAKE_IF_IDLE_LOCK,
			rust_sched_wake_if_idle_select(true, false));
	KUNIT_EXPECT_EQ(test, RUST_SCHED_WAKE_IF_IDLE_LOCK |
			      RUST_SCHED_WAKE_IF_IDLE_RESCHED,
			rust_sched_wake_if_idle_select(true, true));
}

static void sched_wake_test_wake_if_idle_current(struct kunit *test)
{
	migrate_disable();
	wake_up_if_idle(raw_smp_processor_id());
	migrate_enable();
	KUNIT_SUCCEED(test);
}
#endif

#ifdef CONFIG_RUST_SCHED_SELECT_TASK_RQ
static int sched_select_test_calls;
static int sched_select_test_cpu;
static int sched_select_test_flags;
static int sched_select_test_disallowed_cpu;

static int sched_select_test_callback(struct task_struct *task, int cpu,
				      int flags)
{
	sched_select_test_calls++;
	sched_select_test_cpu = cpu;
	sched_select_test_flags = flags;

	return sched_select_test_cpu;
}

static int sched_select_test_disallowed_callback(struct task_struct *task,
						 int cpu, int flags)
{
	sched_select_test_calls++;
	sched_select_test_cpu = cpu;
	sched_select_test_flags = flags;

	return sched_select_test_disallowed_cpu;
}

static void sched_wake_test_select_task_rq_path(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, RUST_SCHED_SELECT_TASK_RQ_CLASS,
			rust_sched_select_task_rq_path_select(2, false));
	KUNIT_EXPECT_EQ(test, RUST_SCHED_SELECT_TASK_RQ_SINGLE,
			rust_sched_select_task_rq_path_select(1, false));
	KUNIT_EXPECT_EQ(test, RUST_SCHED_SELECT_TASK_RQ_SINGLE,
			rust_sched_select_task_rq_path_select(4, true));
}

static void sched_wake_test_select_task_rq_class(struct kunit *test)
{
	struct task_struct *task;
	int initial_flags = 0x10;
	int wake_flags = initial_flags;
	int selected_cpu;
	int cpu;

	task = kzalloc_obj(*task);
	KUNIT_ASSERT_NOT_NULL(test, task);
	raw_spin_lock_init(&task->pi_lock);
	cpumask_clear(&task->cpus_mask);
	selected_cpu = raw_smp_processor_id();
	cpumask_set_cpu(selected_cpu, &task->cpus_mask);
	task->cpus_ptr = &task->cpus_mask;
	task->nr_cpus_allowed = 2;
	task->migration_disabled = 0;

	sched_select_test_calls = 0;
	scoped_guard(raw_spinlock_irqsave, &task->pi_lock)
		cpu = rust_sched_select_task_rq_test(task, selected_cpu,
						     &wake_flags,
						     sched_select_test_callback);

	KUNIT_EXPECT_EQ(test, 1, sched_select_test_calls);
	KUNIT_EXPECT_EQ(test, selected_cpu, sched_select_test_cpu);
	KUNIT_EXPECT_EQ(test, initial_flags, sched_select_test_flags);
	KUNIT_EXPECT_EQ(test, selected_cpu, cpu);
	KUNIT_EXPECT_EQ(test, initial_flags | RUST_SCHED_WF_RQ_SELECTED,
			wake_flags);

	kfree(task);
}

static void sched_wake_test_select_task_rq_single(struct kunit *test)
{
	struct task_struct *task;
	int initial_flags = 0x10;
	int wake_flags;
	int cpu;

	task = kzalloc_obj(*task);
	KUNIT_ASSERT_NOT_NULL(test, task);
	raw_spin_lock_init(&task->pi_lock);
	cpumask_clear(&task->cpus_mask);
	cpumask_set_cpu(raw_smp_processor_id(), &task->cpus_mask);
	task->cpus_ptr = &task->cpus_mask;
	task->nr_cpus_allowed = 1;

	sched_select_test_calls = 0;
	wake_flags = initial_flags;
	scoped_guard(raw_spinlock_irqsave, &task->pi_lock)
		cpu = rust_sched_select_task_rq_test(task, 0, &wake_flags,
						     sched_select_test_callback);
	KUNIT_EXPECT_EQ(test, 0, sched_select_test_calls);
	KUNIT_EXPECT_EQ(test, raw_smp_processor_id(), cpu);
	KUNIT_EXPECT_EQ(test, initial_flags, wake_flags);

	task->nr_cpus_allowed = 4;
	task->migration_disabled = 1;
	sched_select_test_calls = 0;
	wake_flags = initial_flags;
	scoped_guard(raw_spinlock_irqsave, &task->pi_lock)
		cpu = rust_sched_select_task_rq_test(task, 0, &wake_flags,
						     sched_select_test_callback);
	KUNIT_EXPECT_EQ(test, 0, sched_select_test_calls);
	KUNIT_EXPECT_EQ(test, raw_smp_processor_id(), cpu);
	KUNIT_EXPECT_EQ(test, initial_flags, wake_flags);

	kfree(task);
}

static void sched_wake_test_select_task_rq_fallback(struct kunit *test)
{
	struct task_struct *task;
	int initial_flags = 0x10;
	int wake_flags = initial_flags;
	int current_cpu;
	int cpu;

	if (nr_cpu_ids <= 1)
		kunit_skip(test, "fallback test needs two valid CPU indices");

	task = kzalloc_obj(*task);
	KUNIT_ASSERT_NOT_NULL(test, task);
	raw_spin_lock_init(&task->pi_lock);
	cpumask_clear(&task->cpus_mask);
	current_cpu = raw_smp_processor_id();
	cpumask_set_cpu(current_cpu, &task->cpus_mask);
	task->cpus_ptr = &task->cpus_mask;
	task->nr_cpus_allowed = 2;
	task->migration_disabled = 0;
	sched_select_test_disallowed_cpu = (current_cpu + 1) % nr_cpu_ids;

	sched_select_test_calls = 0;
	scoped_guard(raw_spinlock_irqsave, &task->pi_lock) {
		cpu = rust_sched_select_task_rq_test(task, current_cpu,
						     &wake_flags,
						     sched_select_test_disallowed_callback);
	}

	KUNIT_EXPECT_EQ(test, 1, sched_select_test_calls);
	KUNIT_EXPECT_EQ(test, current_cpu, sched_select_test_cpu);
	KUNIT_EXPECT_EQ(test, initial_flags, sched_select_test_flags);
	KUNIT_EXPECT_EQ(test, current_cpu, cpu);
	KUNIT_EXPECT_EQ(test, initial_flags | RUST_SCHED_WF_RQ_SELECTED,
			wake_flags);

	kfree(task);
}
#endif

#ifdef CONFIG_RUST_SCHED_RQ_OPS
static void sched_wake_test_enqueue_task_plan(struct kunit *test)
{
	unsigned int base = RUST_SCHED_RQ_OP_UPDATE_CLOCK |
			    RUST_SCHED_RQ_OP_INFO;
	int no_clock = RUST_SCHED_ENQUEUE_NOCLOCK;
	int restore = RUST_SCHED_ENQUEUE_RESTORE;
	int both = no_clock | restore;

	KUNIT_EXPECT_EQ(test, base,
			rust_sched_enqueue_task_plan_select(0, false));
	KUNIT_EXPECT_EQ(test, RUST_SCHED_RQ_OP_INFO,
			rust_sched_enqueue_task_plan_select(no_clock, false));
	KUNIT_EXPECT_EQ(test, RUST_SCHED_RQ_OP_UPDATE_CLOCK,
			rust_sched_enqueue_task_plan_select(restore, false));
	KUNIT_EXPECT_EQ(test, 0,
			rust_sched_enqueue_task_plan_select(both, false));
	KUNIT_EXPECT_EQ(test, base | RUST_SCHED_RQ_OP_CORE,
			rust_sched_enqueue_task_plan_select(0, true));
}

static void sched_wake_test_dequeue_task_plan(struct kunit *test)
{
	unsigned int base = RUST_SCHED_RQ_OP_UPDATE_CLOCK |
			    RUST_SCHED_RQ_OP_INFO;
	int no_clock = RUST_SCHED_DEQUEUE_NOCLOCK;
	int save = RUST_SCHED_DEQUEUE_SAVE;
	int both = no_clock | save;

	KUNIT_EXPECT_EQ(test, base,
			rust_sched_dequeue_task_plan_select(0, false));
	KUNIT_EXPECT_EQ(test, RUST_SCHED_RQ_OP_INFO,
			rust_sched_dequeue_task_plan_select(no_clock, false));
	KUNIT_EXPECT_EQ(test, RUST_SCHED_RQ_OP_UPDATE_CLOCK,
			rust_sched_dequeue_task_plan_select(save, false));
	KUNIT_EXPECT_EQ(test, 0,
			rust_sched_dequeue_task_plan_select(both, false));
	KUNIT_EXPECT_EQ(test, base | RUST_SCHED_RQ_OP_CORE,
			rust_sched_dequeue_task_plan_select(0, true));
}
#endif

#ifdef CONFIG_RUST_SCHED_CLASS_TRANSITION
static void sched_wake_test_class_transition_plan(struct kunit *test)
{
	unsigned int complete = RUST_SCHED_CLASS_TRANSITION_SERVER |
				RUST_SCHED_CLASS_TRANSITION_PUT |
				RUST_SCHED_CLASS_TRANSITION_SET;

	KUNIT_EXPECT_EQ(test, RUST_SCHED_CLASS_TRANSITION_PUT,
			rust_sched_class_put_prev_plan_select(true));
	KUNIT_EXPECT_EQ(test,
			RUST_SCHED_CLASS_TRANSITION_PUT |
			RUST_SCHED_CLASS_TRANSITION_WARN,
			rust_sched_class_put_prev_plan_select(false));
	KUNIT_EXPECT_EQ(test, RUST_SCHED_CLASS_TRANSITION_SERVER,
			rust_sched_class_put_prev_set_next_plan_select(true, true));
	KUNIT_EXPECT_EQ(test,
			RUST_SCHED_CLASS_TRANSITION_SERVER |
			RUST_SCHED_CLASS_TRANSITION_WARN,
			rust_sched_class_put_prev_set_next_plan_select(false, true));
	KUNIT_EXPECT_EQ(test, complete,
			rust_sched_class_put_prev_set_next_plan_select(true, false));
	KUNIT_EXPECT_EQ(test,
			complete | RUST_SCHED_CLASS_TRANSITION_WARN,
			rust_sched_class_put_prev_set_next_plan_select(false, false));
}
#endif

#ifdef CONFIG_RUST_SCHED_PREV_BALANCE
static void sched_wake_test_prev_balance_plan(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, 0,
			rust_sched_prev_balance_plan_select(false, 0));
	KUNIT_EXPECT_EQ(test, RUST_SCHED_BALANCE_CALL,
			rust_sched_prev_balance_plan_select(true, 0));
	KUNIT_EXPECT_EQ(test,
			RUST_SCHED_BALANCE_CALL | RUST_SCHED_BALANCE_STOP,
			rust_sched_prev_balance_plan_select(true, 1));
	KUNIT_EXPECT_EQ(test, 0,
			rust_sched_prev_balance_plan_select(false, 1));
}
#endif

#if defined(CONFIG_RUST_SCHED_PREV_BALANCE) || \
	defined(CONFIG_RUST_SCHED_PICK_NEXT_TASK)
static void sched_wake_test_next_active_plan(struct kunit *test)
{
	unsigned int advance = RUST_SCHED_NEXT_ACTIVE_ADVANCE;
	unsigned int state;

	KUNIT_EXPECT_EQ(test, advance,
			rust_sched_next_active_plan_select(0, true, false));
	state = RUST_SCHED_SCX_SWITCHED_ALL;
	KUNIT_EXPECT_EQ(test,
			advance | RUST_SCHED_NEXT_ACTIVE_SKIP_FAIR,
			rust_sched_next_active_plan_select(state, true, false));
	KUNIT_EXPECT_EQ(test,
			advance | RUST_SCHED_NEXT_ACTIVE_SKIP_EXT,
			rust_sched_next_active_plan_select(0, false, true));
	state = RUST_SCHED_SCX_ENABLED;
	KUNIT_EXPECT_EQ(test, advance,
			rust_sched_next_active_plan_select(state, false, true));
	state |= RUST_SCHED_SCX_SWITCHED_ALL;
	KUNIT_EXPECT_EQ(test, advance,
			rust_sched_next_active_plan_select(state, false, false));
}
#endif

#ifdef CONFIG_RUST_SCHED_PICK_NEXT_TASK
static void sched_wake_test_pick_fast_path(struct kunit *test)
{
	KUNIT_EXPECT_TRUE(test,
			  rust_sched_pick_fast_counts_select(0, 0));
	KUNIT_EXPECT_TRUE(test,
			  rust_sched_pick_fast_counts_select(7, 7));
	KUNIT_EXPECT_FALSE(test,
			   rust_sched_pick_fast_counts_select(7, 6));
	KUNIT_EXPECT_FALSE(test,
			   rust_sched_pick_fast_counts_select(6, 7));
}

static void sched_wake_test_pick_result_plan(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, RUST_SCHED_PICK_ACTION_RESTART,
			rust_sched_pick_fast_result_select(true, false));
	KUNIT_EXPECT_EQ(test, RUST_SCHED_PICK_ACTION_RESTART,
			rust_sched_pick_fast_result_select(true, true));
	KUNIT_EXPECT_EQ(test, RUST_SCHED_PICK_ACTION_TRANSITION,
			rust_sched_pick_fast_result_select(false, true));
	KUNIT_EXPECT_EQ(test,
			RUST_SCHED_PICK_ACTION_IDLE |
			RUST_SCHED_PICK_ACTION_TRANSITION,
			rust_sched_pick_fast_result_select(false, false));
	KUNIT_EXPECT_EQ(test, RUST_SCHED_PICK_ACTION_RESTART,
			rust_sched_pick_class_result_select(true, false));
	KUNIT_EXPECT_EQ(test, RUST_SCHED_PICK_ACTION_RESTART,
			rust_sched_pick_class_result_select(true, true));
	KUNIT_EXPECT_EQ(test, RUST_SCHED_PICK_ACTION_TRANSITION,
			rust_sched_pick_class_result_select(false, true));
	KUNIT_EXPECT_EQ(test, RUST_SCHED_PICK_ACTION_CONTINUE,
			rust_sched_pick_class_result_select(false, false));
}
#endif

#if defined(CONFIG_RUST_SCHED_TTWU_RUNNABLE) || \
	defined(CONFIG_RUST_SCHED_TTWU_ACTIVATE)
static void sched_wake_test_wakeup_state(struct kunit *test)
{
	struct task_struct *task;

	task = kzalloc_obj(*task);
	KUNIT_ASSERT_NOT_NULL(test, task);

	task->is_blocked = 1;
	task->__state = TASK_INTERRUPTIBLE;
	rust_sched_ttwu_wakeup_state_select(task);
	KUNIT_EXPECT_FALSE(test, task->is_blocked);
	KUNIT_EXPECT_EQ(test, TASK_RUNNING, task->__state);

	kfree(task);
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

static void sched_wake_test_delayed_enqueue_flags(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test,
			RUST_SCHED_ENQUEUE_NOCLOCK |
			RUST_SCHED_ENQUEUE_DELAYED,
			rust_sched_ttwu_delayed_enqueue_flags_select());
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
#ifdef CONFIG_RUST_SCHED_WAKE_PREEMPT
	KUNIT_CASE(sched_wake_test_wakeup_preempt_plan),
#endif
#ifdef CONFIG_RUST_SCHED_RESCHED_CURR
	KUNIT_CASE(sched_wake_test_resched_tif),
	KUNIT_CASE(sched_wake_test_resched_plan),
	KUNIT_CASE(sched_wake_test_resched_remote),
#endif
#ifdef CONFIG_RUST_SCHED_RESCHED_CPU
	KUNIT_CASE(sched_wake_test_resched_cpu),
#endif
#ifdef CONFIG_RUST_SCHED_WAKE_IF_IDLE
	KUNIT_CASE(sched_wake_test_wake_if_idle_plan),
	KUNIT_CASE(sched_wake_test_wake_if_idle_current),
#endif
#ifdef CONFIG_RUST_SCHED_SELECT_TASK_RQ
	KUNIT_CASE(sched_wake_test_select_task_rq_path),
	KUNIT_CASE(sched_wake_test_select_task_rq_class),
	KUNIT_CASE(sched_wake_test_select_task_rq_single),
	KUNIT_CASE(sched_wake_test_select_task_rq_fallback),
#endif
#ifdef CONFIG_RUST_SCHED_RQ_OPS
	KUNIT_CASE(sched_wake_test_enqueue_task_plan),
	KUNIT_CASE(sched_wake_test_dequeue_task_plan),
#endif
#ifdef CONFIG_RUST_SCHED_CLASS_TRANSITION
	KUNIT_CASE(sched_wake_test_class_transition_plan),
#endif
#ifdef CONFIG_RUST_SCHED_PREV_BALANCE
	KUNIT_CASE(sched_wake_test_prev_balance_plan),
#endif
#if defined(CONFIG_RUST_SCHED_PREV_BALANCE) || \
	defined(CONFIG_RUST_SCHED_PICK_NEXT_TASK)
	KUNIT_CASE(sched_wake_test_next_active_plan),
#endif
#ifdef CONFIG_RUST_SCHED_PICK_NEXT_TASK
	KUNIT_CASE(sched_wake_test_pick_fast_path),
	KUNIT_CASE(sched_wake_test_pick_result_plan),
#endif
#ifdef CONFIG_RUST_SCHED_BLOCK_TASK
	KUNIT_CASE(sched_wake_test_block_plan),
	KUNIT_CASE(sched_wake_test_block_task_flags),
	KUNIT_CASE(sched_wake_test_uninterruptible_timeout),
#endif
#ifdef CONFIG_RUST_SCHED_TTWU_RUNNABLE
	KUNIT_CASE(sched_wake_test_runnable_task_flags),
	KUNIT_CASE(sched_wake_test_delayed_enqueue_flags),
#endif
#ifdef CONFIG_RUST_SCHED_RQ_TRANSITION
	KUNIT_CASE(sched_wake_test_rq_transition_plan),
#endif
#if defined(CONFIG_RUST_SCHED_TTWU_RUNNABLE) || \
	defined(CONFIG_RUST_SCHED_TTWU_ACTIVATE)
	KUNIT_CASE(sched_wake_test_wakeup_state),
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
