// SPDX-License-Identifier: GPL-2.0-only

#include <kunit/test.h>

#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/hrtimer.h>
#include <linux/hrtimer_bases.h>
#include <linux/hrtimer-rust.h>
#include <linux/kthread.h>
#include <linux/ktime.h>
#include <linux/timerqueue.h>

enum {
	HRTIMER_CORE_TEST_TIMERS = 3,
};

struct hrtimer_core_test {
	struct hrtimer timer;
	struct completion completion;
	atomic_t callback_count;
};

struct hrtimer_core_test_cancel_wait {
	struct hrtimer_clock_base *base;
	struct completion exit;
};

struct hrtimer_core_test_base_selection {
	struct hrtimer timer;
	struct hrtimer_clock_base new_base;
	struct hrtimer_cpu_base new_cpu_base;
	struct hrtimer_cpu_base this_cpu_base;
};

static enum hrtimer_restart hrtimer_core_test_callback(struct hrtimer *timer)
{
	struct hrtimer_core_test *test;

	test = container_of(timer, struct hrtimer_core_test, timer);
	atomic_inc(&test->callback_count);
	complete(&test->completion);
	return HRTIMER_NORESTART;
}

static int hrtimer_core_test_release_callback(void *data)
{
	struct hrtimer_core_test_cancel_wait *cancel_wait = data;
	unsigned long flags;

	msleep(20);
	raw_spin_lock_irqsave(&cancel_wait->base->cpu_base->lock, flags);
	cancel_wait->base->running = NULL;
	raw_spin_unlock_irqrestore(&cancel_wait->base->cpu_base->lock, flags);
	kthread_complete_and_exit(&cancel_wait->exit, 0);
}

static void hrtimer_core_test_start_pinned(struct hrtimer_core_test *test,
					   ktime_t expires)
{
	hrtimer_setup_on_stack(&test->timer, hrtimer_core_test_callback,
			       CLOCK_MONOTONIC, HRTIMER_MODE_ABS_PINNED);
	hrtimer_start(&test->timer, expires, HRTIMER_MODE_ABS_PINNED);
}

#ifdef CONFIG_RUST_HRTIMER_BASE_SELECTION
static void hrtimer_core_test_base_selection(struct kunit *test)
{
	struct hrtimer_core_test_base_selection *state;
	struct hrtimer_cpu_base *new_cpu_base;
	struct hrtimer_cpu_base *this_cpu_base;
	struct hrtimer_clock_base *new_base;

	state = kunit_kzalloc(test, sizeof(*state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, state);
	new_cpu_base = &state->new_cpu_base;
	this_cpu_base = &state->this_cpu_base;
	new_base = &state->new_base;

	new_base->cpu_base = new_cpu_base;
	new_base->offset = ns_to_ktime(100);
	new_cpu_base->online = true;
	this_cpu_base->online = true;
	hrtimer_set_expires(&state->timer, ns_to_ktime(1000));

	new_cpu_base->expires_next = ns_to_ktime(800);
	KUNIT_EXPECT_TRUE(test,
			  rust_hrtimer_suitable_target(&state->timer, new_base,
						       new_cpu_base,
						       this_cpu_base));

	new_cpu_base->expires_next = ns_to_ktime(900);
	KUNIT_EXPECT_TRUE(test,
			  rust_hrtimer_suitable_target(&state->timer, new_base,
						       new_cpu_base,
						       this_cpu_base));

	new_cpu_base->expires_next = ns_to_ktime(901);
	KUNIT_EXPECT_FALSE(test,
			   rust_hrtimer_suitable_target(&state->timer, new_base,
							new_cpu_base,
							this_cpu_base));

	this_cpu_base->online = false;
	KUNIT_EXPECT_TRUE(test,
			  rust_hrtimer_suitable_target(&state->timer, new_base,
						       new_cpu_base,
						       this_cpu_base));

	this_cpu_base->online = true;
	KUNIT_EXPECT_TRUE(test,
			  rust_hrtimer_suitable_target(&state->timer, new_base,
						       this_cpu_base,
						       this_cpu_base));
}
#endif

static void hrtimer_core_test_assert_order(struct kunit *test,
					   struct hrtimer_core_test *timers,
					   int first, int second, int third)
{
	struct timerqueue_linked_node *node;
	struct hrtimer_clock_base *base = timers[0].timer.base;
	int observed[HRTIMER_CORE_TEST_TIMERS] = { -1, -1, -1 };
	unsigned long flags;
	int count = 0;
	int i;

	raw_spin_lock_irqsave(&base->cpu_base->lock, flags);
	node = timerqueue_linked_first(&base->active);
	while (node && count < ARRAY_SIZE(observed)) {
		for (i = 0; i < HRTIMER_CORE_TEST_TIMERS; i++) {
			if (node == &timers[i].timer.node) {
				observed[count++] = i;
				break;
			}
		}
		node = timerqueue_linked_next(node);
	}
	raw_spin_unlock_irqrestore(&base->cpu_base->lock, flags);

	KUNIT_EXPECT_EQ(test, count, ARRAY_SIZE(observed));
	KUNIT_EXPECT_EQ(test, observed[0], first);
	KUNIT_EXPECT_EQ(test, observed[1], second);
	KUNIT_EXPECT_EQ(test, observed[2], third);
}

static void hrtimer_core_test_same_base_requeue(struct kunit *test)
{
	struct hrtimer_core_test timers[HRTIMER_CORE_TEST_TIMERS];
	ktime_t now = ktime_get();
	int i;

	for (i = 0; i < ARRAY_SIZE(timers); i++) {
		ktime_t expires = ktime_add_ms(now, 100000 + i * 1000);

		hrtimer_core_test_start_pinned(&timers[i], expires);
	}

	for (i = 0; i < ARRAY_SIZE(timers); i++)
		KUNIT_ASSERT_TRUE(test, hrtimer_is_queued(&timers[i].timer));

	hrtimer_core_test_assert_order(test, timers, 0, 1, 2);

	/* First timer stays first and updates in place. */
	hrtimer_start(&timers[0].timer, ktime_add_ms(now, 100500),
		      HRTIMER_MODE_ABS_PINNED);
	KUNIT_ASSERT_TRUE(test, hrtimer_is_queued(&timers[0].timer));
	hrtimer_core_test_assert_order(test, timers, 0, 1, 2);

	/* Middle timer stays between its neighbors and updates in place. */
	hrtimer_start(&timers[1].timer, ktime_add_ms(now, 100750),
		      HRTIMER_MODE_ABS_PINNED);
	KUNIT_ASSERT_TRUE(test, hrtimer_is_queued(&timers[1].timer));
	hrtimer_core_test_assert_order(test, timers, 0, 1, 2);

	/* Moving behind the next timer forces a linked-tree requeue. */
	hrtimer_start(&timers[1].timer, ktime_add_ms(now, 103500),
		      HRTIMER_MODE_ABS_PINNED);
	KUNIT_ASSERT_TRUE(test, hrtimer_is_queued(&timers[1].timer));
	hrtimer_core_test_assert_order(test, timers, 0, 2, 1);

	/* Moving the first timer behind its successor updates the base cache. */
	hrtimer_start(&timers[0].timer, ktime_add_ms(now, 104000),
		      HRTIMER_MODE_ABS_PINNED);
	KUNIT_ASSERT_TRUE(test, hrtimer_is_queued(&timers[0].timer));
	hrtimer_core_test_assert_order(test, timers, 2, 1, 0);

	for (i = 0; i < ARRAY_SIZE(timers); i++) {
		KUNIT_EXPECT_EQ(test, hrtimer_try_to_cancel(&timers[i].timer), 1);
		KUNIT_EXPECT_FALSE(test, hrtimer_is_queued(&timers[i].timer));
		destroy_hrtimer_on_stack(&timers[i].timer);
	}
}

static void hrtimer_core_test_expiry(struct kunit *test)
{
	struct hrtimer_core_test timer;

	atomic_set(&timer.callback_count, 0);
	init_completion(&timer.completion);
	hrtimer_setup_on_stack(&timer.timer, hrtimer_core_test_callback,
			       CLOCK_MONOTONIC, HRTIMER_MODE_REL_PINNED);
	hrtimer_start(&timer.timer, ms_to_ktime(10), HRTIMER_MODE_REL_PINNED);
	KUNIT_ASSERT_TRUE(test, hrtimer_is_queued(&timer.timer));
	KUNIT_ASSERT_NE(test,
			wait_for_completion_timeout(&timer.completion, HZ), 0);
	KUNIT_EXPECT_EQ(test, atomic_read(&timer.callback_count), 1);
	KUNIT_EXPECT_FALSE(test, hrtimer_is_queued(&timer.timer));
	destroy_hrtimer_on_stack(&timer.timer);
}

static void hrtimer_core_test_cancel_states(struct kunit *test)
{
	struct hrtimer_core_test timer;
	struct hrtimer_clock_base *base;
	unsigned long flags;

	hrtimer_core_test_start_pinned(&timer, ktime_add_ms(ktime_get(), 100000));
	base = timer.timer.base;

	raw_spin_lock_irqsave(&base->cpu_base->lock, flags);
	base->running = &timer.timer;
	raw_spin_unlock_irqrestore(&base->cpu_base->lock, flags);

	KUNIT_EXPECT_EQ(test, hrtimer_try_to_cancel(&timer.timer), -1);
	KUNIT_EXPECT_TRUE(test, hrtimer_is_queued(&timer.timer));

	raw_spin_lock_irqsave(&base->cpu_base->lock, flags);
	base->running = NULL;
	raw_spin_unlock_irqrestore(&base->cpu_base->lock, flags);

	KUNIT_EXPECT_EQ(test, hrtimer_try_to_cancel(&timer.timer), 1);
	KUNIT_EXPECT_FALSE(test, hrtimer_is_queued(&timer.timer));
	KUNIT_EXPECT_EQ(test, hrtimer_try_to_cancel(&timer.timer), 0);
	destroy_hrtimer_on_stack(&timer.timer);
}

static void hrtimer_core_test_cancel_wait(struct kunit *test)
{
	struct hrtimer_core_test_cancel_wait cancel_wait;
	struct hrtimer_core_test timer;
	struct task_struct *task;
	struct hrtimer_clock_base *base;
	unsigned long flags;

	hrtimer_core_test_start_pinned(&timer, ktime_add_ms(ktime_get(), 100000));
	KUNIT_ASSERT_TRUE(test, hrtimer_is_queued(&timer.timer));
	KUNIT_EXPECT_EQ(test, hrtimer_cancel(&timer.timer), 1);
	KUNIT_EXPECT_FALSE(test, hrtimer_is_queued(&timer.timer));
	KUNIT_EXPECT_EQ(test, hrtimer_cancel(&timer.timer), 0);

	hrtimer_start(&timer.timer, ktime_add_ms(ktime_get(), 100000),
		      HRTIMER_MODE_ABS_PINNED);
	KUNIT_ASSERT_TRUE(test, hrtimer_is_queued(&timer.timer));
	base = timer.timer.base;

	raw_spin_lock_irqsave(&base->cpu_base->lock, flags);
	base->running = &timer.timer;
	raw_spin_unlock_irqrestore(&base->cpu_base->lock, flags);

	KUNIT_EXPECT_EQ(test, hrtimer_try_to_cancel(&timer.timer), -1);

	cancel_wait.base = base;
	init_completion(&cancel_wait.exit);
	task = kthread_run(hrtimer_core_test_release_callback, &cancel_wait,
			   "hrtimer-cancel-release");
	if (IS_ERR(task)) {
		KUNIT_FAIL(test, "failed to start callback release thread");
		raw_spin_lock_irqsave(&base->cpu_base->lock, flags);
		base->running = NULL;
		raw_spin_unlock_irqrestore(&base->cpu_base->lock, flags);
		KUNIT_EXPECT_EQ(test, hrtimer_cancel(&timer.timer), 1);
		destroy_hrtimer_on_stack(&timer.timer);
		return;
	}

	KUNIT_EXPECT_EQ(test, hrtimer_cancel(&timer.timer), 1);
	wait_for_completion(&cancel_wait.exit);
	KUNIT_EXPECT_FALSE(test, hrtimer_is_queued(&timer.timer));
	destroy_hrtimer_on_stack(&timer.timer);
}

static void hrtimer_core_test_forward(struct kunit *test)
{
	struct hrtimer_core_test timer;
	ktime_t interval = ms_to_ktime(1000);
	ktime_t now = ktime_get();
	ktime_t base;
	unsigned int resolution;

	hrtimer_setup_on_stack(&timer.timer, hrtimer_core_test_callback,
			       CLOCK_MONOTONIC, HRTIMER_MODE_ABS_PINNED);

	hrtimer_set_expires(&timer.timer, ktime_add_ms(now, 1000));
	KUNIT_EXPECT_EQ(test, hrtimer_forward(&timer.timer, now, interval), 0);
	KUNIT_EXPECT_EQ(test, hrtimer_get_expires(&timer.timer),
			ktime_add_ms(now, 1000));
	KUNIT_EXPECT_EQ(test, hrtimer_get_softexpires(&timer.timer),
			ktime_add_ms(now, 1000));

	hrtimer_set_expires(&timer.timer, ktime_sub_ms(now, 2500));
	KUNIT_EXPECT_EQ(test, hrtimer_forward(&timer.timer, now, interval), 3);
	KUNIT_EXPECT_EQ(test, hrtimer_get_expires(&timer.timer),
			ktime_add_ms(now, 500));
	KUNIT_EXPECT_EQ(test, hrtimer_get_softexpires(&timer.timer),
			ktime_add_ms(now, 500));

	hrtimer_set_expires(&timer.timer, ktime_sub_ms(now, 2000));
	KUNIT_EXPECT_EQ(test, hrtimer_forward(&timer.timer, now, interval), 3);
	KUNIT_EXPECT_EQ(test, hrtimer_get_expires(&timer.timer),
			ktime_add_ms(now, 1000));
	KUNIT_EXPECT_EQ(test, hrtimer_get_softexpires(&timer.timer),
			ktime_add_ms(now, 1000));

	resolution = hrtimer_resolution;
	base = ns_to_ktime(1000000000);
	hrtimer_set_expires(&timer.timer, base);
	KUNIT_EXPECT_EQ(test,
			hrtimer_forward(&timer.timer,
					ktime_add_ns(base, 2 * resolution),
					ns_to_ktime(resolution / 2)), 3);
	KUNIT_EXPECT_EQ(test, hrtimer_get_expires(&timer.timer),
			ktime_add_ns(base, 3 * resolution));
	KUNIT_EXPECT_EQ(test, hrtimer_get_softexpires(&timer.timer),
			ktime_add_ns(base, 3 * resolution));

	destroy_hrtimer_on_stack(&timer.timer);
}

static struct kunit_case hrtimer_core_test_cases[] = {
#ifdef CONFIG_RUST_HRTIMER_BASE_SELECTION
	KUNIT_CASE(hrtimer_core_test_base_selection),
#endif
	KUNIT_CASE(hrtimer_core_test_same_base_requeue),
	KUNIT_CASE(hrtimer_core_test_expiry),
	KUNIT_CASE(hrtimer_core_test_cancel_states),
	KUNIT_CASE(hrtimer_core_test_cancel_wait),
	KUNIT_CASE(hrtimer_core_test_forward),
	{}
};

static struct kunit_suite hrtimer_core_test_suite = {
	.name = "hrtimer_core",
	.test_cases = hrtimer_core_test_cases,
};

kunit_test_suite(hrtimer_core_test_suite);

MODULE_DESCRIPTION("KUnit tests for hrtimer queue and cancellation state");
MODULE_LICENSE("GPL");
