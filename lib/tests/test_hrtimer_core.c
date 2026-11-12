// SPDX-License-Identifier: GPL-2.0-only

#include <kunit/test.h>

#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/hrtimer.h>
#include <linux/hrtimer_bases.h>
#include <linux/hrtimer-rust.h>
#include <linux/kthread.h>
#include <linux/ktime.h>
#include <linux/string.h>
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

struct hrtimer_core_test_next_event {
	struct hrtimer_cpu_base cpu_base;
	struct hrtimer timers[2];
};

struct hrtimer_core_test_reprogram {
	struct hrtimer timer;
	struct hrtimer_cpu_base cpu_base;
	struct hrtimer_cpu_base remote_cpu_base;
};

struct hrtimer_core_test_enqueue {
	struct hrtimer_cpu_base cpu_base;
	struct hrtimer timers[3];
};

struct hrtimer_core_test_callback {
	struct hrtimer_cpu_base cpu_base;
	struct hrtimer timer;
};

struct hrtimer_core_test_base_migration {
	struct hrtimer_cpu_base this_cpu_base;
	struct hrtimer_cpu_base new_cpu_base;
	struct hrtimer timer;
};

struct hrtimer_core_test_target_base {
	struct hrtimer_cpu_base base;
	struct hrtimer_cpu_base offline;
	struct hrtimer_cpu_base nohz;
};

struct hrtimer_core_test_keep_base {
	struct hrtimer_cpu_base cpu_base;
	struct hrtimer timer;
};

struct hrtimer_core_test_run_queues {
	struct hrtimer_clock_base base;
	struct hrtimer timers[2];
};

#ifdef CONFIG_RUST_HRTIMER_INTERRUPT
struct hrtimer_core_test_interrupt {
	struct hrtimer first;
	struct hrtimer second;
	struct completion completion;
	atomic_t second_calls;
};
#endif

struct hrtimer_core_test_start {
	struct hrtimer timer;
};

static enum hrtimer_restart hrtimer_core_test_callback(struct hrtimer *timer)
{
	struct hrtimer_core_test *test;

	test = container_of(timer, struct hrtimer_core_test, timer);
	atomic_inc(&test->callback_count);
	complete(&test->completion);
	return HRTIMER_NORESTART;
}

#ifdef CONFIG_RUST_HRTIMER_INTERRUPT
static enum hrtimer_restart
hrtimer_core_test_interrupt_first(struct hrtimer *timer)
{
	struct hrtimer_core_test_interrupt *state;

	state = container_of(timer, struct hrtimer_core_test_interrupt, first);
	hrtimer_start(&state->second,
		      ktime_sub_ms(ktime_get_boottime(), 1),
		      HRTIMER_MODE_ABS_PINNED);
	return HRTIMER_NORESTART;
}

static enum hrtimer_restart
hrtimer_core_test_interrupt_second(struct hrtimer *timer)
{
	struct hrtimer_core_test_interrupt *state;

	state = container_of(timer, struct hrtimer_core_test_interrupt, second);
	atomic_inc(&state->second_calls);
	complete(&state->completion);
	return HRTIMER_NORESTART;
}
#endif

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

#ifdef CONFIG_RUST_HRTIMER_NEXT_EVENT
static void
hrtimer_core_test_init_active_base(struct hrtimer_cpu_base *cpu_base,
				   unsigned int index, struct hrtimer *timer,
				   ktime_t expires, ktime_t offset)
{
	struct hrtimer_clock_base *base = &cpu_base->clock_base[index];

	timerqueue_linked_init(&timer->node);
	timer->node.expires = expires;
	timerqueue_linked_init_head(&base->active);
	base->cpu_base = cpu_base;
	base->expires_next = expires;
	base->offset = offset;
	timerqueue_linked_add(&base->active, &timer->node);
	cpu_base->active_bases |= 1U << index;
}

static void hrtimer_core_test_next_event(struct kunit *test)
{
	struct hrtimer_core_test_next_event *state;
	struct hrtimer_cpu_base *cpu_base;
	ktime_t expires;

	state = kunit_kzalloc(test, sizeof(*state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, state);
	cpu_base = &state->cpu_base;

	hrtimer_core_test_init_active_base(cpu_base, HRTIMER_BASE_MONOTONIC,
					   &state->timers[0],
					   ns_to_ktime(1000), ns_to_ktime(100));
	hrtimer_core_test_init_active_base(cpu_base, HRTIMER_BASE_REALTIME,
					   &state->timers[1],
					   ns_to_ktime(950), ns_to_ktime(0));
	expires = rust_hrtimer_get_next_event(cpu_base,
					      (1U << HRTIMER_BASE_MONOTONIC) |
					      (1U << HRTIMER_BASE_REALTIME));
	KUNIT_EXPECT_EQ(test, ns_to_ktime(900), expires);
	KUNIT_EXPECT_PTR_EQ(test, &state->timers[0], cpu_base->next_timer);
	KUNIT_EXPECT_PTR_EQ(test, NULL, cpu_base->softirq_next_timer);

	memset(state, 0, sizeof(*state));
	hrtimer_core_test_init_active_base(cpu_base,
					   HRTIMER_BASE_MONOTONIC_SOFT,
					   &state->timers[0],
					   ns_to_ktime(850), ns_to_ktime(50));
	expires = rust_hrtimer_get_next_event(cpu_base,
					      1U << HRTIMER_BASE_MONOTONIC_SOFT);
	KUNIT_EXPECT_EQ(test, ns_to_ktime(800), expires);
	KUNIT_EXPECT_PTR_EQ(test, &state->timers[0],
			    cpu_base->softirq_next_timer);
	KUNIT_EXPECT_PTR_EQ(test, NULL, cpu_base->next_timer);

	memset(state, 0, sizeof(*state));
	hrtimer_core_test_init_active_base(cpu_base, HRTIMER_BASE_MONOTONIC,
					   &state->timers[0],
					   ns_to_ktime(900), ns_to_ktime(0));
	hrtimer_core_test_init_active_base(cpu_base,
					   HRTIMER_BASE_MONOTONIC_SOFT,
					   &state->timers[1],
					   ns_to_ktime(800), ns_to_ktime(0));
	expires = rust_hrtimer_get_next_event(cpu_base,
					      (1U << HRTIMER_BASE_MONOTONIC) |
					      (1U << HRTIMER_BASE_MONOTONIC_SOFT));
	KUNIT_EXPECT_EQ(test, ns_to_ktime(800), expires);
	KUNIT_EXPECT_PTR_EQ(test, &state->timers[1], cpu_base->next_timer);
	KUNIT_EXPECT_PTR_EQ(test, &state->timers[1],
			    cpu_base->softirq_next_timer);

	memset(state, 0, sizeof(*state));
	hrtimer_core_test_init_active_base(cpu_base, HRTIMER_BASE_MONOTONIC,
					   &state->timers[0],
					   ns_to_ktime(800), ns_to_ktime(0));
	hrtimer_core_test_init_active_base(cpu_base,
					   HRTIMER_BASE_MONOTONIC_SOFT,
					   &state->timers[1],
					   ns_to_ktime(800), ns_to_ktime(0));
	expires = rust_hrtimer_get_next_event(cpu_base,
					      (1U << HRTIMER_BASE_MONOTONIC) |
					      (1U << HRTIMER_BASE_MONOTONIC_SOFT));
	KUNIT_EXPECT_EQ(test, ns_to_ktime(800), expires);
	KUNIT_EXPECT_PTR_EQ(test, &state->timers[1], cpu_base->next_timer);

	memset(state, 0, sizeof(*state));
	hrtimer_core_test_init_active_base(cpu_base, HRTIMER_BASE_MONOTONIC,
					   &state->timers[0],
					   ns_to_ktime(900), ns_to_ktime(0));
	hrtimer_core_test_init_active_base(cpu_base,
					   HRTIMER_BASE_MONOTONIC_SOFT,
					   &state->timers[1],
					   ns_to_ktime(800), ns_to_ktime(0));
	cpu_base->softirq_activated = true;
	expires = rust_hrtimer_get_next_event(cpu_base,
					      (1U << HRTIMER_BASE_MONOTONIC) |
					      (1U << HRTIMER_BASE_MONOTONIC_SOFT));
	KUNIT_EXPECT_EQ(test, ns_to_ktime(900), expires);
	KUNIT_EXPECT_PTR_EQ(test, &state->timers[0], cpu_base->next_timer);
	KUNIT_EXPECT_PTR_EQ(test, NULL, cpu_base->softirq_next_timer);

	memset(state, 0, sizeof(*state));
	hrtimer_core_test_init_active_base(cpu_base, HRTIMER_BASE_MONOTONIC,
					   &state->timers[0],
					   ns_to_ktime(50), ns_to_ktime(100));
	expires = rust_hrtimer_get_next_event(cpu_base,
					      1U << HRTIMER_BASE_MONOTONIC);
	KUNIT_EXPECT_EQ(test, ns_to_ktime(0), expires);
	KUNIT_EXPECT_PTR_EQ(test, &state->timers[0], cpu_base->next_timer);

	memset(state, 0, sizeof(*state));
	expires = rust_hrtimer_get_next_event(cpu_base, ~0U);
	KUNIT_EXPECT_EQ(test, ns_to_ktime(KTIME_MAX), expires);
	KUNIT_EXPECT_PTR_EQ(test, NULL, cpu_base->next_timer);
	KUNIT_EXPECT_PTR_EQ(test, NULL, cpu_base->softirq_next_timer);
}
#endif

#ifdef CONFIG_RUST_HRTIMER_UPDATE_NEXT_EVENT
static void hrtimer_core_test_update_next_event(struct kunit *test)
{
	struct hrtimer_core_test_next_event *state;
	struct hrtimer_cpu_base *cpu_base;
	ktime_t expires;

	state = kunit_kzalloc(test, sizeof(*state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, state);
	cpu_base = &state->cpu_base;

	hrtimer_core_test_init_active_base(cpu_base, HRTIMER_BASE_MONOTONIC,
					   &state->timers[0],
					   ns_to_ktime(900), ns_to_ktime(0));
	hrtimer_core_test_init_active_base(cpu_base,
					   HRTIMER_BASE_MONOTONIC_SOFT,
					   &state->timers[1],
					   ns_to_ktime(800), ns_to_ktime(0));
	expires = rust_hrtimer_update_next_event(cpu_base);
	KUNIT_EXPECT_EQ(test, ns_to_ktime(800), expires);
	KUNIT_EXPECT_EQ(test, ns_to_ktime(800),
			cpu_base->softirq_expires_next);
	KUNIT_EXPECT_PTR_EQ(test, &state->timers[1], cpu_base->next_timer);

	memset(state, 0, sizeof(*state));
	hrtimer_core_test_init_active_base(cpu_base, HRTIMER_BASE_MONOTONIC,
					   &state->timers[0],
					   ns_to_ktime(800), ns_to_ktime(0));
	hrtimer_core_test_init_active_base(cpu_base,
					   HRTIMER_BASE_MONOTONIC_SOFT,
					   &state->timers[1],
					   ns_to_ktime(900), ns_to_ktime(0));
	expires = rust_hrtimer_update_next_event(cpu_base);
	KUNIT_EXPECT_EQ(test, ns_to_ktime(800), expires);
	KUNIT_EXPECT_EQ(test, ns_to_ktime(900),
			cpu_base->softirq_expires_next);
	KUNIT_EXPECT_PTR_EQ(test, &state->timers[0], cpu_base->next_timer);

	memset(state, 0, sizeof(*state));
	hrtimer_core_test_init_active_base(cpu_base, HRTIMER_BASE_MONOTONIC,
					   &state->timers[0],
					   ns_to_ktime(800), ns_to_ktime(0));
	hrtimer_core_test_init_active_base(cpu_base,
					   HRTIMER_BASE_MONOTONIC_SOFT,
					   &state->timers[1],
					   ns_to_ktime(800), ns_to_ktime(0));
	expires = rust_hrtimer_update_next_event(cpu_base);
	KUNIT_EXPECT_EQ(test, ns_to_ktime(800), expires);
	KUNIT_EXPECT_PTR_EQ(test, &state->timers[0], cpu_base->next_timer);
	KUNIT_EXPECT_PTR_EQ(test, &state->timers[1],
			    cpu_base->softirq_next_timer);

	memset(state, 0, sizeof(*state));
	hrtimer_core_test_init_active_base(cpu_base, HRTIMER_BASE_MONOTONIC,
					   &state->timers[0],
					   ns_to_ktime(900), ns_to_ktime(0));
	hrtimer_core_test_init_active_base(cpu_base,
					   HRTIMER_BASE_MONOTONIC_SOFT,
					   &state->timers[1],
					   ns_to_ktime(800), ns_to_ktime(0));
	cpu_base->softirq_activated = true;
	cpu_base->softirq_expires_next = ns_to_ktime(1234);
	expires = rust_hrtimer_update_next_event(cpu_base);
	KUNIT_EXPECT_EQ(test, ns_to_ktime(900), expires);
	KUNIT_EXPECT_EQ(test, ns_to_ktime(1234),
			cpu_base->softirq_expires_next);
	KUNIT_EXPECT_PTR_EQ(test, &state->timers[0], cpu_base->next_timer);
	KUNIT_EXPECT_PTR_EQ(test, NULL, cpu_base->softirq_next_timer);

	memset(state, 0, sizeof(*state));
	expires = rust_hrtimer_update_next_event(cpu_base);
	KUNIT_EXPECT_EQ(test, ns_to_ktime(KTIME_MAX), expires);
	KUNIT_EXPECT_EQ(test, ns_to_ktime(KTIME_MAX),
			cpu_base->softirq_expires_next);
	KUNIT_EXPECT_PTR_EQ(test, NULL, cpu_base->next_timer);
}
#endif

#ifdef CONFIG_RUST_HRTIMER_UPDATE_NEEDS_IPI
static void hrtimer_core_test_update_needs_ipi(struct kunit *test)
{
	struct hrtimer_core_test_next_event *state;
	struct hrtimer_cpu_base *cpu_base;
	unsigned int active;

	state = kunit_kzalloc(test, sizeof(*state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, state);
	cpu_base = &state->cpu_base;
	active = 1U << HRTIMER_BASE_MONOTONIC;

	hrtimer_core_test_init_active_base(cpu_base, HRTIMER_BASE_MONOTONIC,
					   &state->timers[0],
					   ns_to_ktime(100), ns_to_ktime(0));
	cpu_base->clock_was_set_seq = 1;
	cpu_base->expires_next = ns_to_ktime(200);
	KUNIT_EXPECT_FALSE(test,
			   rust_hrtimer_update_needs_ipi(cpu_base, active, 1));
	KUNIT_EXPECT_TRUE(test,
			  rust_hrtimer_update_needs_ipi(cpu_base, active, 0));

	memset(state, 0, sizeof(*state));
	hrtimer_core_test_init_active_base(cpu_base, HRTIMER_BASE_MONOTONIC,
					   &state->timers[0],
					   ns_to_ktime(100), ns_to_ktime(0));
	cpu_base->clock_was_set_seq = 1;
	cpu_base->expires_next = ns_to_ktime(200);
	KUNIT_EXPECT_FALSE(test,
			   rust_hrtimer_update_needs_ipi(cpu_base,
							 1U << HRTIMER_BASE_REALTIME,
							 0));

	memset(state, 0, sizeof(*state));
	hrtimer_core_test_init_active_base(cpu_base, HRTIMER_BASE_MONOTONIC,
					   &state->timers[0],
					   ns_to_ktime(200), ns_to_ktime(0));
	cpu_base->clock_was_set_seq = 1;
	cpu_base->expires_next = ns_to_ktime(200);
	KUNIT_EXPECT_FALSE(test,
			   rust_hrtimer_update_needs_ipi(cpu_base, active, 0));

	memset(state, 0, sizeof(*state));
	hrtimer_core_test_init_active_base(cpu_base, HRTIMER_BASE_MONOTONIC,
					   &state->timers[0],
					   ns_to_ktime(250), ns_to_ktime(100));
	cpu_base->clock_was_set_seq = 1;
	cpu_base->expires_next = ns_to_ktime(200);
	KUNIT_EXPECT_TRUE(test,
			  rust_hrtimer_update_needs_ipi(cpu_base, active, 0));

	memset(state, 0, sizeof(*state));
	hrtimer_core_test_init_active_base(cpu_base, HRTIMER_BASE_MONOTONIC,
					   &state->timers[0],
					   ns_to_ktime(100), ns_to_ktime(0));
	cpu_base->clock_was_set_seq = 1;
	cpu_base->expires_next = ns_to_ktime(200);
	cpu_base->deferred_rearm = true;
	KUNIT_EXPECT_FALSE(test,
			   rust_hrtimer_update_needs_ipi(cpu_base, active, 0));
	KUNIT_EXPECT_TRUE(test, cpu_base->deferred_needs_update);

	memset(state, 0, sizeof(*state));
	active = 1U << HRTIMER_BASE_MONOTONIC_SOFT;
	hrtimer_core_test_init_active_base(cpu_base,
					   HRTIMER_BASE_MONOTONIC_SOFT,
					   &state->timers[0],
					   ns_to_ktime(800), ns_to_ktime(0));
	cpu_base->clock_was_set_seq = 1;
	cpu_base->expires_next = ns_to_ktime(700);
	cpu_base->softirq_expires_next = ns_to_ktime(900);
	KUNIT_EXPECT_TRUE(test,
			  rust_hrtimer_update_needs_ipi(cpu_base, active, 0));

	memset(state, 0, sizeof(*state));
	hrtimer_core_test_init_active_base(cpu_base,
					   HRTIMER_BASE_MONOTONIC_SOFT,
					   &state->timers[0],
					   ns_to_ktime(800), ns_to_ktime(0));
	cpu_base->clock_was_set_seq = 1;
	cpu_base->expires_next = ns_to_ktime(700);
	cpu_base->softirq_expires_next = ns_to_ktime(900);
	cpu_base->softirq_activated = true;
	KUNIT_EXPECT_FALSE(test,
			   rust_hrtimer_update_needs_ipi(cpu_base, active, 0));

	memset(state, 0, sizeof(*state));
	hrtimer_core_test_init_active_base(cpu_base,
					   HRTIMER_BASE_MONOTONIC_SOFT,
					   &state->timers[0],
					   ns_to_ktime(800), ns_to_ktime(0));
	cpu_base->clock_was_set_seq = 1;
	cpu_base->expires_next = ns_to_ktime(700);
	cpu_base->softirq_expires_next = ns_to_ktime(700);
	KUNIT_EXPECT_FALSE(test,
			   rust_hrtimer_update_needs_ipi(cpu_base, active, 0));
}
#endif

#ifdef CONFIG_RUST_HRTIMER_REPROGRAM
static void
hrtimer_core_test_init_reprogram(struct hrtimer_core_test_reprogram *state,
				 struct hrtimer_cpu_base *base_cpu,
				 ktime_t expires, ktime_t offset)
{
	struct hrtimer_clock_base *base =
		&state->cpu_base.clock_base[HRTIMER_BASE_MONOTONIC];

	base->cpu_base = base_cpu;
	base->offset = offset;
	state->timer.base = base;
	hrtimer_set_expires(&state->timer, expires);
}

static void hrtimer_core_test_reprogram(struct kunit *test)
{
	struct hrtimer_core_test_reprogram *state;
	struct hrtimer_cpu_base *cpu_base;
	bool program_device;
	ktime_t expires;

	state = kunit_kzalloc(test, sizeof(*state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, state);
	cpu_base = &state->cpu_base;

	hrtimer_core_test_init_reprogram(state, cpu_base, ns_to_ktime(250),
					 ns_to_ktime(100));
	cpu_base->expires_next = ns_to_ktime(200);
	expires = rust_hrtimer_reprogram(&state->timer, cpu_base, true,
					 &program_device);
	KUNIT_EXPECT_EQ(test, ns_to_ktime(150), expires);
	KUNIT_EXPECT_TRUE(test, program_device);
	KUNIT_EXPECT_PTR_EQ(test, &state->timer, cpu_base->next_timer);

	memset(state, 0, sizeof(*state));
	hrtimer_core_test_init_reprogram(state, cpu_base, ns_to_ktime(50),
					 ns_to_ktime(100));
	cpu_base->expires_next = ns_to_ktime(200);
	expires = rust_hrtimer_reprogram(&state->timer, cpu_base, true,
					 &program_device);
	KUNIT_EXPECT_EQ(test, ns_to_ktime(0), expires);
	KUNIT_EXPECT_TRUE(test, program_device);
	KUNIT_EXPECT_PTR_EQ(test, &state->timer, cpu_base->next_timer);

	memset(state, 0, sizeof(*state));
	hrtimer_core_test_init_reprogram(state, cpu_base, ns_to_ktime(200),
					 ns_to_ktime(0));
	cpu_base->expires_next = ns_to_ktime(200);
	expires = rust_hrtimer_reprogram(&state->timer, cpu_base, true,
					 &program_device);
	KUNIT_EXPECT_EQ(test, ns_to_ktime(200), expires);
	KUNIT_EXPECT_FALSE(test, program_device);
	KUNIT_EXPECT_PTR_EQ(test, NULL, cpu_base->next_timer);

	memset(state, 0, sizeof(*state));
	hrtimer_core_test_init_reprogram(state, cpu_base, ns_to_ktime(300),
					 ns_to_ktime(0));
	cpu_base->expires_next = ns_to_ktime(200);
	expires = rust_hrtimer_reprogram(&state->timer, cpu_base, true,
					 &program_device);
	KUNIT_EXPECT_EQ(test, ns_to_ktime(300), expires);
	KUNIT_EXPECT_FALSE(test, program_device);
	KUNIT_EXPECT_PTR_EQ(test, NULL, cpu_base->next_timer);

	memset(state, 0, sizeof(*state));
	hrtimer_core_test_init_reprogram(state, &state->remote_cpu_base,
					 ns_to_ktime(100), ns_to_ktime(0));
	cpu_base->expires_next = ns_to_ktime(200);
	expires = rust_hrtimer_reprogram(&state->timer, cpu_base, true,
					 &program_device);
	KUNIT_EXPECT_EQ(test, ns_to_ktime(100), expires);
	KUNIT_EXPECT_FALSE(test, program_device);
	KUNIT_EXPECT_PTR_EQ(test, NULL, cpu_base->next_timer);

	memset(state, 0, sizeof(*state));
	hrtimer_core_test_init_reprogram(state, cpu_base, ns_to_ktime(100),
					 ns_to_ktime(0));
	cpu_base->expires_next = ns_to_ktime(200);
	cpu_base->deferred_rearm = true;
	expires = rust_hrtimer_reprogram(&state->timer, cpu_base, true,
					 &program_device);
	KUNIT_EXPECT_EQ(test, ns_to_ktime(100), expires);
	KUNIT_EXPECT_FALSE(test, program_device);
	KUNIT_EXPECT_PTR_EQ(test, NULL, cpu_base->next_timer);

	memset(state, 0, sizeof(*state));
	state->timer.is_soft = true;
	hrtimer_core_test_init_reprogram(state, cpu_base, ns_to_ktime(150),
					 ns_to_ktime(50));
	cpu_base->expires_next = ns_to_ktime(300);
	cpu_base->softirq_expires_next = ns_to_ktime(200);
	expires = rust_hrtimer_reprogram(&state->timer, cpu_base, true,
					 &program_device);
	KUNIT_EXPECT_EQ(test, ns_to_ktime(100), expires);
	KUNIT_EXPECT_TRUE(test, program_device);
	KUNIT_EXPECT_PTR_EQ(test, &state->timer,
			    cpu_base->softirq_next_timer);
	KUNIT_EXPECT_PTR_EQ(test, &state->timer, cpu_base->next_timer);
	KUNIT_EXPECT_EQ(test, ns_to_ktime(100),
			cpu_base->softirq_expires_next);

	memset(state, 0, sizeof(*state));
	state->timer.is_soft = true;
	hrtimer_core_test_init_reprogram(state, cpu_base, ns_to_ktime(100),
					 ns_to_ktime(0));
	cpu_base->expires_next = ns_to_ktime(300);
	cpu_base->softirq_expires_next = ns_to_ktime(100);
	expires = rust_hrtimer_reprogram(&state->timer, cpu_base, true,
					 &program_device);
	KUNIT_EXPECT_EQ(test, ns_to_ktime(100), expires);
	KUNIT_EXPECT_FALSE(test, program_device);
	KUNIT_EXPECT_PTR_EQ(test, NULL, cpu_base->softirq_next_timer);
	KUNIT_EXPECT_PTR_EQ(test, NULL, cpu_base->next_timer);

	memset(state, 0, sizeof(*state));
	state->timer.is_soft = true;
	hrtimer_core_test_init_reprogram(state, cpu_base, ns_to_ktime(100),
					 ns_to_ktime(0));
	cpu_base->expires_next = ns_to_ktime(300);
	cpu_base->softirq_expires_next = ns_to_ktime(200);
	cpu_base->softirq_activated = true;
	expires = rust_hrtimer_reprogram(&state->timer, cpu_base, true,
					 &program_device);
	KUNIT_EXPECT_EQ(test, ns_to_ktime(100), expires);
	KUNIT_EXPECT_FALSE(test, program_device);
	KUNIT_EXPECT_PTR_EQ(test, NULL, cpu_base->softirq_next_timer);
	KUNIT_EXPECT_PTR_EQ(test, NULL, cpu_base->next_timer);
	KUNIT_EXPECT_EQ(test, ns_to_ktime(200),
			cpu_base->softirq_expires_next);

	memset(state, 0, sizeof(*state));
	state->timer.is_soft = true;
	hrtimer_core_test_init_reprogram(state, cpu_base, ns_to_ktime(100),
					 ns_to_ktime(0));
	cpu_base->expires_next = ns_to_ktime(300);
	cpu_base->softirq_expires_next = ns_to_ktime(200);
	expires = rust_hrtimer_reprogram(&state->timer, cpu_base, false,
					 &program_device);
	KUNIT_EXPECT_EQ(test, ns_to_ktime(100), expires);
	KUNIT_EXPECT_FALSE(test, program_device);
	KUNIT_EXPECT_PTR_EQ(test, &state->timer,
			    cpu_base->softirq_next_timer);
	KUNIT_EXPECT_PTR_EQ(test, NULL, cpu_base->next_timer);
	KUNIT_EXPECT_EQ(test, ns_to_ktime(100),
			cpu_base->softirq_expires_next);

	memset(state, 0, sizeof(*state));
	state->timer.is_soft = true;
	hrtimer_core_test_init_reprogram(state, cpu_base, ns_to_ktime(250),
					 ns_to_ktime(0));
	cpu_base->expires_next = ns_to_ktime(200);
	cpu_base->softirq_expires_next = ns_to_ktime(300);
	expires = rust_hrtimer_reprogram(&state->timer, cpu_base, true,
					 &program_device);
	KUNIT_EXPECT_EQ(test, ns_to_ktime(250), expires);
	KUNIT_EXPECT_FALSE(test, program_device);
	KUNIT_EXPECT_PTR_EQ(test, &state->timer,
			    cpu_base->softirq_next_timer);
	KUNIT_EXPECT_PTR_EQ(test, NULL, cpu_base->next_timer);
	KUNIT_EXPECT_EQ(test, ns_to_ktime(250),
			cpu_base->softirq_expires_next);

	memset(state, 0, sizeof(*state));
	state->timer.is_soft = true;
	hrtimer_core_test_init_reprogram(state, &state->remote_cpu_base,
					 ns_to_ktime(100), ns_to_ktime(0));
	state->remote_cpu_base.expires_next = ns_to_ktime(300);
	state->remote_cpu_base.softirq_expires_next = ns_to_ktime(200);
	expires = rust_hrtimer_reprogram(&state->timer, cpu_base, true,
					 &program_device);
	KUNIT_EXPECT_EQ(test, ns_to_ktime(100), expires);
	KUNIT_EXPECT_FALSE(test, program_device);
	KUNIT_EXPECT_PTR_EQ(test, &state->timer,
			    state->remote_cpu_base.softirq_next_timer);
	KUNIT_EXPECT_PTR_EQ(test, NULL, cpu_base->next_timer);
	KUNIT_EXPECT_EQ(test, ns_to_ktime(100),
			state->remote_cpu_base.softirq_expires_next);
}
#endif

#ifdef CONFIG_RUST_HRTIMER_ENQUEUE
static void hrtimer_core_test_enqueue(struct kunit *test)
{
	struct hrtimer_core_test_enqueue *state;
	struct hrtimer_cpu_base *cpu_base;
	struct hrtimer_clock_base *base;
	unsigned long flags;
	int i;

	state = kunit_kzalloc(test, sizeof(*state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, state);
	cpu_base = &state->cpu_base;
	base = &cpu_base->clock_base[HRTIMER_BASE_MONOTONIC];
	raw_spin_lock_init(&cpu_base->lock);
	cpu_base->online = true;
	base->cpu_base = cpu_base;
	base->expires_next = ns_to_ktime(KTIME_MAX);
	timerqueue_linked_init_head(&base->active);

	for (i = 0; i < ARRAY_SIZE(state->timers); i++) {
		hrtimer_setup_on_stack(&state->timers[i],
				       hrtimer_core_test_callback,
				       CLOCK_MONOTONIC,
				       HRTIMER_MODE_ABS_PINNED);
		state->timers[i].base = base;
	}
	hrtimer_set_expires(&state->timers[0], ns_to_ktime(200));
	hrtimer_set_expires(&state->timers[1], ns_to_ktime(300));
	hrtimer_set_expires(&state->timers[2], ns_to_ktime(100));

	raw_spin_lock_irqsave(&cpu_base->lock, flags);
	KUNIT_EXPECT_TRUE(test,
			  rust_hrtimer_enqueue(&state->timers[0], base,
					       HRTIMER_MODE_ABS, false));
	KUNIT_EXPECT_EQ(test, ns_to_ktime(200), base->expires_next);
	KUNIT_EXPECT_PTR_EQ(test, &state->timers[0].node,
			    timerqueue_linked_first(&base->active));
	KUNIT_EXPECT_FALSE(test,
			   rust_hrtimer_enqueue(&state->timers[1], base,
						HRTIMER_MODE_ABS, false));
	KUNIT_EXPECT_EQ(test, ns_to_ktime(200), base->expires_next);
	KUNIT_EXPECT_TRUE(test,
			  rust_hrtimer_enqueue(&state->timers[2], base,
					       HRTIMER_MODE_ABS, false));
	KUNIT_EXPECT_EQ(test, ns_to_ktime(100), base->expires_next);
	KUNIT_EXPECT_PTR_EQ(test, &state->timers[2].node,
			    timerqueue_linked_first(&base->active));
	KUNIT_EXPECT_EQ(test, 1U << HRTIMER_BASE_MONOTONIC,
			cpu_base->active_bases);
	for (i = 0; i < ARRAY_SIZE(state->timers); i++)
		KUNIT_EXPECT_TRUE(test, state->timers[i].is_queued);
	raw_spin_unlock_irqrestore(&cpu_base->lock, flags);

	raw_spin_lock_irqsave(&cpu_base->lock, flags);
	for (i = 0; i < ARRAY_SIZE(state->timers); i++) {
		rust_helper_hrtimer_debug_deactivate(&state->timers[i]);
		timerqueue_linked_del(&base->active, &state->timers[i].node);
		rust_helper_hrtimer_set_queued(&state->timers[i], false);
	}
	raw_spin_unlock_irqrestore(&cpu_base->lock, flags);

	for (i = 0; i < ARRAY_SIZE(state->timers); i++)
		destroy_hrtimer_on_stack(&state->timers[i]);
}
#endif

#ifdef CONFIG_RUST_HRTIMER_CALLBACK
static void
hrtimer_core_test_init_callback(struct hrtimer_core_test_callback *state)
{
	struct hrtimer_cpu_base *cpu_base = &state->cpu_base;
	struct hrtimer_clock_base *base;

	raw_spin_lock_init(&cpu_base->lock);
	seqcount_raw_spinlock_init(&cpu_base->clock_base[HRTIMER_BASE_MONOTONIC].seq,
				   &cpu_base->lock);
	cpu_base->online = true;
	base = &cpu_base->clock_base[HRTIMER_BASE_MONOTONIC];
	base->cpu_base = cpu_base;
	base->expires_next = ns_to_ktime(KTIME_MAX);
	timerqueue_linked_init_head(&base->active);
	hrtimer_setup_on_stack(&state->timer, hrtimer_core_test_callback,
			       CLOCK_MONOTONIC, HRTIMER_MODE_ABS_PINNED);
	state->timer.base = base;
	hrtimer_set_expires(&state->timer, ns_to_ktime(100));
}

static void
hrtimer_core_test_enqueue_callback_timer(struct kunit *test,
					 struct hrtimer_core_test_callback *state)
{
	struct hrtimer_clock_base *base =
		&state->cpu_base.clock_base[HRTIMER_BASE_MONOTONIC];

	KUNIT_EXPECT_TRUE(test,
			  rust_hrtimer_enqueue(&state->timer, base,
					       HRTIMER_MODE_ABS, false));
}

static void
hrtimer_core_test_cleanup_callback(struct hrtimer_core_test_callback *state)
{
	struct hrtimer_cpu_base *cpu_base = &state->cpu_base;
	struct hrtimer_clock_base *base =
		&cpu_base->clock_base[HRTIMER_BASE_MONOTONIC];
	unsigned long flags;

	raw_spin_lock_irqsave(&cpu_base->lock, flags);
	if (state->timer.is_queued) {
		rust_helper_hrtimer_debug_deactivate(&state->timer);
		rust_hrtimer_remove(&state->timer, base, false, false);
	}
	raw_spin_unlock_irqrestore(&cpu_base->lock, flags);
	destroy_hrtimer_on_stack(&state->timer);
}

static void hrtimer_core_test_callback_lifecycle(struct kunit *test)
{
	struct hrtimer_core_test_callback *state;
	struct hrtimer_clock_base *base;
	unsigned long flags;

	state = kunit_kzalloc(test, sizeof(*state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, state);
	base = &state->cpu_base.clock_base[HRTIMER_BASE_MONOTONIC];

	hrtimer_core_test_init_callback(state);
	raw_spin_lock_irqsave(&state->cpu_base.lock, flags);
	hrtimer_core_test_enqueue_callback_timer(test, state);
	rust_helper_hrtimer_debug_deactivate(&state->timer);
	rust_hrtimer_callback_enter(&state->timer, base);
	KUNIT_EXPECT_PTR_EQ(test, &state->timer, base->running);
	KUNIT_EXPECT_FALSE(test, state->timer.is_queued);
	KUNIT_EXPECT_EQ(test, 0U, state->cpu_base.active_bases);
	rust_hrtimer_callback_exit(&state->timer, base, HRTIMER_RESTART);
	KUNIT_EXPECT_TRUE(test, state->timer.is_queued);
	KUNIT_EXPECT_PTR_EQ(test, NULL, base->running);
	KUNIT_EXPECT_EQ(test, 1U << HRTIMER_BASE_MONOTONIC,
			state->cpu_base.active_bases);
	raw_spin_unlock_irqrestore(&state->cpu_base.lock, flags);
	hrtimer_core_test_cleanup_callback(state);

	memset(state, 0, sizeof(*state));
	hrtimer_core_test_init_callback(state);
	raw_spin_lock_irqsave(&state->cpu_base.lock, flags);
	hrtimer_core_test_enqueue_callback_timer(test, state);
	rust_helper_hrtimer_debug_deactivate(&state->timer);
	rust_hrtimer_callback_enter(&state->timer, base);
	rust_hrtimer_callback_exit(&state->timer, base, HRTIMER_NORESTART);
	KUNIT_EXPECT_FALSE(test, state->timer.is_queued);
	KUNIT_EXPECT_PTR_EQ(test, NULL, base->running);
	KUNIT_EXPECT_EQ(test, 0U, state->cpu_base.active_bases);
	raw_spin_unlock_irqrestore(&state->cpu_base.lock, flags);
	hrtimer_core_test_cleanup_callback(state);

	memset(state, 0, sizeof(*state));
	hrtimer_core_test_init_callback(state);
	raw_spin_lock_irqsave(&state->cpu_base.lock, flags);
	hrtimer_core_test_enqueue_callback_timer(test, state);
	rust_helper_hrtimer_debug_deactivate(&state->timer);
	rust_hrtimer_callback_enter(&state->timer, base);
	KUNIT_EXPECT_TRUE(test,
			  rust_hrtimer_enqueue(&state->timer, base,
					       HRTIMER_MODE_ABS, false));
	rust_hrtimer_callback_exit(&state->timer, base, HRTIMER_RESTART);
	KUNIT_EXPECT_TRUE(test, state->timer.is_queued);
	KUNIT_EXPECT_PTR_EQ(test, NULL, base->running);
	KUNIT_EXPECT_PTR_EQ(test, &state->timer.node,
			    timerqueue_linked_first(&base->active));
	raw_spin_unlock_irqrestore(&state->cpu_base.lock, flags);
	hrtimer_core_test_cleanup_callback(state);
}
#endif

#ifdef CONFIG_RUST_HRTIMER_BASE_MIGRATION
static void
hrtimer_core_test_init_migration_cpu(struct hrtimer_cpu_base *cpu_base)
{
	struct hrtimer_clock_base initial = {
		.index = HRTIMER_BASE_MONOTONIC,
		.clockid = CLOCK_MONOTONIC,
	};
	struct hrtimer_clock_base *base;

	raw_spin_lock_init(&cpu_base->lock);
	base = &cpu_base->clock_base[HRTIMER_BASE_MONOTONIC];
	memcpy(base, &initial, sizeof(initial));
	base->cpu_base = cpu_base;
	cpu_base->online = true;
	cpu_base->expires_next = ns_to_ktime(KTIME_MAX);
	timerqueue_linked_init_head(&base->active);
}

static void hrtimer_core_test_base_migration(struct kunit *test)
{
	struct hrtimer_core_test_base_migration *state;
	struct hrtimer_clock_base *base, *new_base, *selected;
	unsigned long flags;

	state = kunit_kzalloc(test, sizeof(*state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, state);
	hrtimer_core_test_init_migration_cpu(&state->this_cpu_base);
	hrtimer_core_test_init_migration_cpu(&state->new_cpu_base);
	base = &state->this_cpu_base.clock_base[HRTIMER_BASE_MONOTONIC];
	new_base = &state->new_cpu_base.clock_base[HRTIMER_BASE_MONOTONIC];

	hrtimer_setup_on_stack(&state->timer, hrtimer_core_test_callback,
			       CLOCK_MONOTONIC, HRTIMER_MODE_ABS_PINNED);
	hrtimer_set_expires(&state->timer, ns_to_ktime(100));
	state->timer.base = base;
	state->new_cpu_base.expires_next = ns_to_ktime(50);
	raw_spin_lock_irqsave(&base->cpu_base->lock, flags);
	selected = rust_hrtimer_switch_base_to(&state->timer, base,
					       &state->this_cpu_base,
					       &state->new_cpu_base);
	KUNIT_EXPECT_PTR_EQ(test, new_base, selected);
	KUNIT_EXPECT_PTR_EQ(test, new_base, state->timer.base);
	raw_spin_unlock_irqrestore(&selected->cpu_base->lock, flags);

	state->timer.base = base;
	base->running = &state->timer;
	raw_spin_lock_irqsave(&base->cpu_base->lock, flags);
	selected = rust_hrtimer_switch_base_to(&state->timer, base,
					       &state->this_cpu_base,
					       &state->new_cpu_base);
	KUNIT_EXPECT_PTR_EQ(test, base, selected);
	KUNIT_EXPECT_PTR_EQ(test, base, state->timer.base);
	raw_spin_unlock_irqrestore(&selected->cpu_base->lock, flags);
	base->running = NULL;

	state->timer.base = base;
	state->new_cpu_base.expires_next = ns_to_ktime(KTIME_MAX);
	raw_spin_lock_irqsave(&base->cpu_base->lock, flags);
	selected = rust_hrtimer_switch_base_to(&state->timer, base,
					       &state->this_cpu_base,
					       &state->new_cpu_base);
	KUNIT_EXPECT_PTR_EQ(test, base, selected);
	KUNIT_EXPECT_PTR_EQ(test, base, state->timer.base);
	raw_spin_unlock_irqrestore(&selected->cpu_base->lock, flags);

	destroy_hrtimer_on_stack(&state->timer);
}
#endif

#ifdef CONFIG_RUST_HRTIMER_TARGET_BASE
static void hrtimer_core_test_target_base(struct kunit *test)
{
	struct hrtimer_core_test_target_base *state;
	struct hrtimer_cpu_base *selected;

	state = kunit_kzalloc(test, sizeof(*state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, state);

	selected = rust_hrtimer_target_base_select(&state->base, false,
						   &state->offline,
						   &state->nohz, true, false);
	KUNIT_EXPECT_PTR_EQ(test, &state->base, selected);
	selected = rust_hrtimer_target_base_select(&state->base, false,
						   &state->offline,
						   &state->nohz, true, true);
	KUNIT_EXPECT_PTR_EQ(test, &state->nohz, selected);
	selected = rust_hrtimer_target_base_select(&state->base, true,
						   &state->offline,
						   &state->nohz, true, true);
	KUNIT_EXPECT_PTR_EQ(test, &state->base, selected);
	selected = rust_hrtimer_target_base_select(&state->base, false,
						   &state->offline,
						   &state->nohz, false, true);
	KUNIT_EXPECT_PTR_EQ(test, &state->offline, selected);
	selected = rust_hrtimer_target_base_select(&state->base, true,
						   &state->offline,
						   &state->nohz, false, false);
	KUNIT_EXPECT_PTR_EQ(test, &state->offline, selected);
}
#endif

#ifdef CONFIG_RUST_HRTIMER_KEEP_BASE
static void hrtimer_core_test_keep_base(struct kunit *test)
{
	struct hrtimer_core_test_keep_base *state;
	struct hrtimer_clock_base *base;

	state = kunit_kzalloc(test, sizeof(*state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, state);
	base = &state->cpu_base.clock_base[HRTIMER_BASE_MONOTONIC];
	base->cpu_base = &state->cpu_base;

	hrtimer_setup_on_stack(&state->timer, hrtimer_core_test_callback,
			       CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
	state->timer.base = base;

	base->running = &state->timer;
	KUNIT_EXPECT_TRUE(test,
			  rust_hrtimer_keep_base(&state->timer, false, false,
						 false));
	base->running = NULL;

	KUNIT_EXPECT_FALSE(test,
			   rust_hrtimer_keep_base(&state->timer, false, false,
						  false));
	KUNIT_EXPECT_TRUE(test,
			  rust_hrtimer_keep_base(&state->timer, true, true,
						 false));
	KUNIT_EXPECT_TRUE(test,
			  rust_hrtimer_keep_base(&state->timer, true, false,
						 true));

	KUNIT_EXPECT_TRUE(test,
			  rust_hrtimer_prefer_local_select(false, true, false,
							   false, false, true,
							   false));
	KUNIT_EXPECT_FALSE(test,
			   rust_hrtimer_prefer_local_select(false, false, true,
							    true, true, false,
							    true));
	KUNIT_EXPECT_FALSE(test,
			   rust_hrtimer_prefer_local_select(true, false, true,
							    true, true, false,
							    true));
	KUNIT_EXPECT_TRUE(test,
			  rust_hrtimer_prefer_local_select(true, true, true,
							   false, false, true,
							   false));
	KUNIT_EXPECT_TRUE(test,
			  rust_hrtimer_prefer_local_select(true, true, false,
							   true, false, true,
							   false));
	KUNIT_EXPECT_FALSE(test,
			   rust_hrtimer_prefer_local_select(true, true, false,
							    false, false, false,
							    false));
	KUNIT_EXPECT_TRUE(test,
			  rust_hrtimer_prefer_local_select(true, true, false,
							   false, true, false,
							   false));
	KUNIT_EXPECT_FALSE(test,
			   rust_hrtimer_prefer_local_select(true, true, false,
							    false, true, true,
							    false));
	KUNIT_EXPECT_TRUE(test,
			  rust_hrtimer_prefer_local_select(true, true, false,
							   false, true, true,
							   true));

	destroy_hrtimer_on_stack(&state->timer);
}
#endif

#ifdef CONFIG_RUST_HRTIMER_RUN_QUEUES
static void hrtimer_core_test_run_queues(struct kunit *test)
{
	struct hrtimer_core_test_run_queues *state;
	struct hrtimer_clock_base *base;
	int i;

	state = kunit_kzalloc(test, sizeof(*state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, state);
	base = &state->base;
	timerqueue_linked_init_head(&base->active);

	for (i = 0; i < ARRAY_SIZE(state->timers); i++) {
		hrtimer_setup_on_stack(&state->timers[i],
				       hrtimer_core_test_callback,
				       CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
		state->timers[i].base = base;
	}
	hrtimer_set_expires(&state->timers[0], ns_to_ktime(200));
	hrtimer_set_expires(&state->timers[1], ns_to_ktime(300));
	timerqueue_linked_add(&base->active, &state->timers[0].node);
	timerqueue_linked_add(&base->active, &state->timers[1].node);

	KUNIT_EXPECT_PTR_EQ(test, NULL,
			    rust_hrtimer_clock_base_next_expired(base,
								 ns_to_ktime(199)));
	KUNIT_EXPECT_PTR_EQ(test, &state->timers[0],
			    rust_hrtimer_clock_base_next_expired(base,
								 ns_to_ktime(200)));
	KUNIT_EXPECT_PTR_EQ(test, &state->timers[0],
			    rust_hrtimer_clock_base_next_expired(base,
								 ns_to_ktime(250)));

	timerqueue_linked_del(&base->active, &state->timers[0].node);
	KUNIT_EXPECT_PTR_EQ(test, &state->timers[1],
			    rust_hrtimer_clock_base_next_expired(base,
								 ns_to_ktime(300)));
	timerqueue_linked_del(&base->active, &state->timers[1].node);
	KUNIT_EXPECT_PTR_EQ(test, NULL,
			    rust_hrtimer_clock_base_next_expired(base,
								 ns_to_ktime(KTIME_MAX)));

	for (i = 0; i < ARRAY_SIZE(state->timers); i++)
		destroy_hrtimer_on_stack(&state->timers[i]);
}

static void hrtimer_core_test_soft_expiry(struct kunit *test)
{
	struct hrtimer_core_test timer;

	atomic_set(&timer.callback_count, 0);
	init_completion(&timer.completion);
	hrtimer_setup_on_stack(&timer.timer, hrtimer_core_test_callback,
			       CLOCK_MONOTONIC, HRTIMER_MODE_REL_SOFT);
	hrtimer_start(&timer.timer, ms_to_ktime(10), HRTIMER_MODE_REL_SOFT);
	KUNIT_ASSERT_TRUE(test, hrtimer_is_queued(&timer.timer));
	KUNIT_ASSERT_NE(test,
			wait_for_completion_timeout(&timer.completion, HZ), 0);
	KUNIT_EXPECT_EQ(test, atomic_read(&timer.callback_count), 1);
	KUNIT_EXPECT_FALSE(test, hrtimer_is_queued(&timer.timer));
	destroy_hrtimer_on_stack(&timer.timer);
}
#endif

#ifdef CONFIG_RUST_HRTIMER_INTERRUPT
static void hrtimer_core_test_interrupt(struct kunit *test)
{
	struct hrtimer_cpu_base cpu_base = { };
	struct hrtimer_core_test_interrupt *state;
	unsigned int cpu;
	unsigned int retries;
	unsigned int retries_after;
	unsigned int retries_before;

	retries = 7;
	KUNIT_EXPECT_EQ(test, RUST_HRTIMER_INTERRUPT_DONE,
			rust_hrtimer_interrupt_select(ns_to_ktime(200),
						      ns_to_ktime(100), 0,
						      &retries));
	KUNIT_EXPECT_EQ(test, 0, retries);
	KUNIT_EXPECT_EQ(test, RUST_HRTIMER_INTERRUPT_DONE,
			rust_hrtimer_interrupt_select(ns_to_ktime(100),
						      ns_to_ktime(100), 0,
						      &retries));
	KUNIT_EXPECT_EQ(test, 0, retries);
	KUNIT_EXPECT_EQ(test, RUST_HRTIMER_INTERRUPT_RETRY,
			rust_hrtimer_interrupt_select(ns_to_ktime(100),
						      ns_to_ktime(200), 0,
						      &retries));
	KUNIT_EXPECT_EQ(test, 1, retries);
	KUNIT_EXPECT_EQ(test, RUST_HRTIMER_INTERRUPT_RETRY,
			rust_hrtimer_interrupt_select(ns_to_ktime(100),
						      ns_to_ktime(200), 1,
						      &retries));
	KUNIT_EXPECT_EQ(test, 2, retries);
	KUNIT_EXPECT_EQ(test, RUST_HRTIMER_INTERRUPT_HANG,
			rust_hrtimer_interrupt_select(ns_to_ktime(100),
						      ns_to_ktime(200), 2,
						      &retries));
	KUNIT_EXPECT_EQ(test, 3, retries);

	cpu_base.max_hang_time = 50;
	KUNIT_EXPECT_EQ(test, RUST_HRTIMER_INTERRUPT_RETRY,
			rust_hrtimer_interrupt_advance(&cpu_base,
						       ns_to_ktime(100),
						       ns_to_ktime(200),
						       ns_to_ktime(75), 0,
						       &retries));
	KUNIT_EXPECT_EQ(test, 1, retries);
	KUNIT_EXPECT_EQ(test, 1, cpu_base.nr_retries);
	KUNIT_EXPECT_EQ(test, 50, cpu_base.max_hang_time);
	KUNIT_EXPECT_FALSE(test, cpu_base.hang_detected);
	KUNIT_EXPECT_EQ(test, RUST_HRTIMER_INTERRUPT_RETRY,
			rust_hrtimer_interrupt_advance(&cpu_base,
						       ns_to_ktime(100),
						       ns_to_ktime(200),
						       ns_to_ktime(75), 1,
						       &retries));
	KUNIT_EXPECT_EQ(test, 2, retries);
	KUNIT_EXPECT_EQ(test, 2, cpu_base.nr_retries);
	KUNIT_EXPECT_EQ(test, RUST_HRTIMER_INTERRUPT_HANG,
			rust_hrtimer_interrupt_advance(&cpu_base,
						       ns_to_ktime(100),
						       ns_to_ktime(200),
						       ns_to_ktime(75), 2,
						       &retries));
	KUNIT_EXPECT_EQ(test, 3, retries);
	KUNIT_EXPECT_EQ(test, 125, cpu_base.max_hang_time);
	KUNIT_EXPECT_EQ(test, 1, cpu_base.nr_hangs);
	KUNIT_EXPECT_TRUE(test, cpu_base.hang_detected);

	KUNIT_EXPECT_FALSE(test,
			   rust_hrtimer_interrupt_softirq_due(ns_to_ktime(99),
							      ns_to_ktime(100)));
	KUNIT_EXPECT_TRUE(test,
			  rust_hrtimer_interrupt_softirq_due(ns_to_ktime(100),
							     ns_to_ktime(100)));
	KUNIT_EXPECT_TRUE(test,
			  rust_hrtimer_interrupt_softirq_due(ns_to_ktime(101),
							     ns_to_ktime(100)));

	KUNIT_EXPECT_EQ(test, ns_to_ktime(1000),
			rust_hrtimer_interrupt_hang_expiry(ns_to_ktime(1000),
							   0));
	KUNIT_EXPECT_EQ(test, ns_to_ktime(6000),
			rust_hrtimer_interrupt_hang_expiry(ns_to_ktime(1000),
							   5000));
	KUNIT_EXPECT_EQ(test, ns_to_ktime(100001000),
			rust_hrtimer_interrupt_hang_expiry(ns_to_ktime(1000),
							   100 * NSEC_PER_MSEC));
	KUNIT_EXPECT_EQ(test, ns_to_ktime(100001000),
			rust_hrtimer_interrupt_hang_expiry(ns_to_ktime(1000),
							   101 * NSEC_PER_MSEC));

	state = kunit_kzalloc(test, sizeof(*state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, state);
	init_completion(&state->completion);
	hrtimer_setup_on_stack(&state->first, hrtimer_core_test_interrupt_first,
			       CLOCK_MONOTONIC, HRTIMER_MODE_ABS_PINNED);
	hrtimer_setup_on_stack(&state->second, hrtimer_core_test_interrupt_second,
			       CLOCK_BOOTTIME, HRTIMER_MODE_ABS_PINNED);

	get_cpu();
	cpu = smp_processor_id();
	retries_before = rust_hrtimer_interrupt_test_retries(cpu);
	hrtimer_start(&state->first, ktime_add_ms(ktime_get(), 1),
		      HRTIMER_MODE_ABS_PINNED);
	put_cpu();
	KUNIT_ASSERT_NE(test,
			wait_for_completion_timeout(&state->completion, HZ), 0);
	retries_after = rust_hrtimer_interrupt_test_retries(cpu);
	if (!IS_ENABLED(CONFIG_PREEMPT_RT))
		KUNIT_EXPECT_GT(test, retries_after, retries_before);
	KUNIT_EXPECT_EQ(test, atomic_read(&state->second_calls), 1);
	KUNIT_EXPECT_FALSE(test, hrtimer_is_queued(&state->first));
	KUNIT_EXPECT_FALSE(test, hrtimer_is_queued(&state->second));
	destroy_hrtimer_on_stack(&state->first);
	destroy_hrtimer_on_stack(&state->second);
}
#endif

#ifdef CONFIG_RUST_HRTIMER_START
static void hrtimer_core_test_start(struct kunit *test)
{
	struct hrtimer_core_test_start *state;
	ktime_t expected;

	state = kunit_kzalloc(test, sizeof(*state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, state);
	hrtimer_setup_on_stack(&state->timer, hrtimer_core_test_callback,
			       CLOCK_MONOTONIC, HRTIMER_MODE_ABS);

	KUNIT_EXPECT_EQ(test,
			rust_hrtimer_prepare_expiry(&state->timer, ns_to_ktime(50),
						    HRTIMER_MODE_ABS,
						    ns_to_ktime(100)),
			ns_to_ktime(50));
	expected = ns_to_ktime(150);
#ifdef CONFIG_TIME_LOW_RES
	expected = ktime_add(expected, hrtimer_resolution);
	KUNIT_EXPECT_FALSE(test, state->timer.is_rel);
#endif
	KUNIT_EXPECT_EQ(test,
			rust_hrtimer_prepare_expiry(&state->timer, ns_to_ktime(50),
						    HRTIMER_MODE_REL,
						    ns_to_ktime(100)), expected);
#ifdef CONFIG_TIME_LOW_RES
	KUNIT_EXPECT_TRUE(test, state->timer.is_rel);
#endif

	KUNIT_EXPECT_EQ(test,
			rust_hrtimer_start_reprogram_select(true, false, false,
							    false, false, false),
			RUST_HRTIMER_START_ACTION_DEFERRED);
	KUNIT_EXPECT_EQ(test,
			rust_hrtimer_start_reprogram_select(false, false, true,
							    true, false, false),
			RUST_HRTIMER_START_ACTION_NONE);
	KUNIT_EXPECT_EQ(test,
			rust_hrtimer_start_reprogram_select(false, false, true,
							    true, true, false),
			RUST_HRTIMER_START_ACTION_REPROGRAM);
	KUNIT_EXPECT_EQ(test,
			rust_hrtimer_start_reprogram_select(false, false, false,
							    true, true, false),
			RUST_HRTIMER_START_ACTION_REPROGRAM);
	KUNIT_EXPECT_EQ(test,
			rust_hrtimer_start_reprogram_select(false, false, false,
							    false, false, false),
			RUST_HRTIMER_START_ACTION_NONE);
	KUNIT_EXPECT_EQ(test,
			rust_hrtimer_start_reprogram_select(false, false, false,
							    false, true, false),
			RUST_HRTIMER_START_ACTION_REMOTE_WAKE);
	KUNIT_EXPECT_EQ(test,
			rust_hrtimer_start_reprogram_select(false, true, true,
							    true, true, true),
			RUST_HRTIMER_START_ACTION_NONE);
	KUNIT_EXPECT_EQ(test,
			rust_hrtimer_start_reprogram_select(false, true, true,
							    true, true, false),
			RUST_HRTIMER_START_ACTION_FORCE);

	destroy_hrtimer_on_stack(&state->timer);
}
#endif

#ifdef CONFIG_RUST_HRTIMER_USER_EXPIRY
static void hrtimer_core_test_user_expiry(struct kunit *test)
{
	struct hrtimer_core_test timer;
	bool ret;

	KUNIT_EXPECT_TRUE(test,
			  rust_hrtimer_user_expiry_select(ns_to_ktime(150),
							  ns_to_ktime(100),
							  ns_to_ktime(60),
							  ns_to_ktime(40)));
	KUNIT_EXPECT_FALSE(test,
			   rust_hrtimer_user_expiry_select(ns_to_ktime(150),
							   ns_to_ktime(100),
							   ns_to_ktime(60),
							   ns_to_ktime(50)));
	KUNIT_EXPECT_TRUE(test,
			  rust_hrtimer_user_expiry_select(ns_to_ktime(150),
							  ns_to_ktime(100),
							  ns_to_ktime(50),
							  ns_to_ktime(100)));
	KUNIT_EXPECT_FALSE(test,
			   rust_hrtimer_user_expiry_select(ns_to_ktime(50),
							   ns_to_ktime(100),
							   ns_to_ktime(0),
							   ns_to_ktime(0)));

	atomic_set(&timer.callback_count, 0);
	init_completion(&timer.completion);
	hrtimer_setup_on_stack(&timer.timer, hrtimer_core_test_callback,
			       CLOCK_MONOTONIC, HRTIMER_MODE_ABS_PINNED);

	ret = hrtimer_start_range_ns_user(&timer.timer,
					  ktime_sub_ms(ktime_get(), 1), 0,
					  HRTIMER_MODE_ABS_PINNED);
	KUNIT_EXPECT_FALSE(test, ret);
	KUNIT_EXPECT_FALSE(test, hrtimer_is_queued(&timer.timer));

	ret = hrtimer_start_range_ns_user(&timer.timer,
					  ktime_add_ms(ktime_get(), 100000), 0,
					  HRTIMER_MODE_ABS_PINNED);
	KUNIT_EXPECT_TRUE(test, ret);
	KUNIT_EXPECT_TRUE(test, hrtimer_is_queued(&timer.timer));
	KUNIT_EXPECT_EQ(test, hrtimer_cancel(&timer.timer), 1);
	destroy_hrtimer_on_stack(&timer.timer);
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
#ifdef CONFIG_RUST_HRTIMER_NEXT_EVENT
	KUNIT_CASE(hrtimer_core_test_next_event),
#endif
#ifdef CONFIG_RUST_HRTIMER_UPDATE_NEXT_EVENT
	KUNIT_CASE(hrtimer_core_test_update_next_event),
#endif
#ifdef CONFIG_RUST_HRTIMER_UPDATE_NEEDS_IPI
	KUNIT_CASE(hrtimer_core_test_update_needs_ipi),
#endif
#ifdef CONFIG_RUST_HRTIMER_REPROGRAM
	KUNIT_CASE(hrtimer_core_test_reprogram),
#endif
#ifdef CONFIG_RUST_HRTIMER_ENQUEUE
	KUNIT_CASE(hrtimer_core_test_enqueue),
#endif
#ifdef CONFIG_RUST_HRTIMER_CALLBACK
	KUNIT_CASE(hrtimer_core_test_callback_lifecycle),
#endif
#ifdef CONFIG_RUST_HRTIMER_RUN_QUEUES
	KUNIT_CASE(hrtimer_core_test_run_queues),
	KUNIT_CASE(hrtimer_core_test_soft_expiry),
#endif
#ifdef CONFIG_RUST_HRTIMER_INTERRUPT
	KUNIT_CASE(hrtimer_core_test_interrupt),
#endif
#ifdef CONFIG_RUST_HRTIMER_START
	KUNIT_CASE(hrtimer_core_test_start),
#endif
#ifdef CONFIG_RUST_HRTIMER_USER_EXPIRY
	KUNIT_CASE(hrtimer_core_test_user_expiry),
#endif
#ifdef CONFIG_RUST_HRTIMER_BASE_MIGRATION
	KUNIT_CASE(hrtimer_core_test_base_migration),
#endif
#ifdef CONFIG_RUST_HRTIMER_TARGET_BASE
	KUNIT_CASE(hrtimer_core_test_target_base),
#endif
#ifdef CONFIG_RUST_HRTIMER_KEEP_BASE
	KUNIT_CASE(hrtimer_core_test_keep_base),
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
