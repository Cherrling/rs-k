// SPDX-License-Identifier: GPL-2.0-only

#include <kunit/test.h>

#include <linux/errno.h>
#include <linux/hrtimer.h>
#include <linux/itimer-rust.h>
#include <linux/posix-timers.h>
#include <linux/sched/signal.h>
#include <linux/time64.h>

#define ITIMER_TEST_LONG_SEC	3600
#define ITIMER_TEST_PERIOD_SEC	3

struct itimer_test_state {
	struct itimerspec64 saved[3];
};

static bool itimer_test_spec_zero(const struct itimerspec64 *value)
{
	return !value->it_value.tv_sec && !value->it_value.tv_nsec &&
	       !value->it_interval.tv_sec && !value->it_interval.tv_nsec;
}

static void itimer_test_restore(void *data)
{
	struct itimer_test_state *state = data;
	int which;

	for (which = 0; which < 3; which++)
		itimer_test_set(which, &state->saved[which], NULL);
}

static struct itimer_test_state *itimer_test_prepare(struct kunit *test)
{
	struct itimer_test_state *state;
	int which;

	state = kunit_kzalloc(test, sizeof(*state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, state);

	for (which = 0; which < 3; which++) {
		int ret = itimer_test_get(which, &state->saved[which]);

		KUNIT_ASSERT_EQ(test, ret, 0);
	}

	KUNIT_ASSERT_EQ(test,
			kunit_add_action_or_reset(test, itimer_test_restore, state),
			0);
	return state;
}

static void itimer_test_invalid_clock(struct kunit *test)
{
	struct itimerspec64 value = {};

	KUNIT_EXPECT_EQ(test, itimer_test_get(3, &value), -EINVAL);
	KUNIT_EXPECT_EQ(test, itimer_test_set(3, &value, NULL), -EINVAL);
	KUNIT_EXPECT_EQ(test, itimer_test_get(-1, &value), -EINVAL);
	KUNIT_EXPECT_EQ(test, itimer_test_set(-1, &value, NULL), -EINVAL);
}

static void itimer_test_real_lifecycle(struct kunit *test)
{
	struct itimer_test_state *state;
	struct itimerspec64 value = {};
	struct itimerspec64 old = {};
	struct itimerspec64 got = {};

	state = itimer_test_prepare(test);

	value.it_value.tv_sec = 10;
	value.it_interval.tv_sec = 2;
	KUNIT_ASSERT_EQ(test, itimer_test_set(ITIMER_REAL, &value, &old), 0);

	KUNIT_ASSERT_EQ(test, itimer_test_get(ITIMER_REAL, &got), 0);
	KUNIT_EXPECT_EQ(test, got.it_interval.tv_sec, 2);
	KUNIT_EXPECT_EQ(test, got.it_interval.tv_nsec, 0);
	KUNIT_EXPECT_GE(test, got.it_value.tv_sec, 0);
	KUNIT_EXPECT_LE(test, got.it_value.tv_sec, 10);
	KUNIT_EXPECT_FALSE(test,
			   !got.it_value.tv_sec && !got.it_value.tv_nsec);

	value = (struct itimerspec64){};
	KUNIT_ASSERT_EQ(test, itimer_test_set(ITIMER_REAL, &value, &old), 0);
	KUNIT_EXPECT_EQ(test, old.it_interval.tv_sec, 2);
	KUNIT_ASSERT_EQ(test, itimer_test_get(ITIMER_REAL, &got), 0);
	KUNIT_EXPECT_TRUE(test, itimer_test_spec_zero(&got));

	KUNIT_EXPECT_EQ(test, itimer_test_set(ITIMER_REAL, &state->saved[ITIMER_REAL], NULL), 0);
}

static void itimer_test_cpu_lifecycle(struct kunit *test)
{
	struct itimer_test_state *state;
	struct itimerspec64 value = {};
	struct itimerspec64 got = {};
	int which;

	state = itimer_test_prepare(test);

	for (which = ITIMER_VIRTUAL; which <= ITIMER_PROF; which++) {
		value = (struct itimerspec64){};
		value.it_value.tv_sec = ITIMER_TEST_LONG_SEC;
		value.it_interval.tv_sec = ITIMER_TEST_PERIOD_SEC;

		KUNIT_ASSERT_EQ(test, itimer_test_set(which, &value, NULL), 0);
		KUNIT_ASSERT_EQ(test, itimer_test_get(which, &got), 0);
		KUNIT_EXPECT_EQ(test, got.it_interval.tv_sec,
				ITIMER_TEST_PERIOD_SEC);
		KUNIT_EXPECT_EQ(test, got.it_interval.tv_nsec, 0);
		KUNIT_EXPECT_GE(test, got.it_value.tv_sec, 0);
		KUNIT_EXPECT_LE(test, got.it_value.tv_sec,
				ITIMER_TEST_LONG_SEC);
		KUNIT_EXPECT_FALSE(test,
				   !got.it_value.tv_sec &&
				   !got.it_value.tv_nsec);

		value = (struct itimerspec64){};
		KUNIT_ASSERT_EQ(test, itimer_test_set(which, &value, NULL), 0);
		KUNIT_ASSERT_EQ(test, itimer_test_get(which, &got), 0);
		KUNIT_EXPECT_TRUE(test, itimer_test_spec_zero(&got));
	}

	for (which = 0; which < 3; which++)
		KUNIT_EXPECT_EQ(test,
				itimer_test_set(which, &state->saved[which], NULL),
				0);
}

static void itimer_test_repeated_replacement(struct kunit *test)
{
	struct itimer_test_state *state;
	struct itimerspec64 value = {};
	struct itimerspec64 zero = {};
	int i;

	state = itimer_test_prepare(test);
	value.it_value.tv_sec = ITIMER_TEST_LONG_SEC;

	for (i = 0; i < 128; i++) {
		int which = i % 3;

		KUNIT_ASSERT_EQ(test, itimer_test_set(which, &value, NULL), 0);
		KUNIT_ASSERT_EQ(test, itimer_test_set(which, &zero, NULL), 0);
		if (!(i & 15))
			cond_resched();
	}

	for (i = 0; i < 3; i++)
		KUNIT_EXPECT_EQ(test, itimer_test_set(i, &state->saved[i], NULL), 0);
}

static void itimer_test_periodic_rearm(struct kunit *test)
{
	struct itimer_test_state *state;
	struct itimerspec64 value = {};
	struct itimerspec64 zero = {};
	struct hrtimer *timer;
	int ret;

	state = itimer_test_prepare(test);
	value.it_value.tv_sec = ITIMER_TEST_LONG_SEC;
	value.it_interval.tv_sec = ITIMER_TEST_PERIOD_SEC;
	KUNIT_ASSERT_EQ(test, itimer_test_set(ITIMER_REAL, &value, NULL), 0);

	timer = &current->signal->real_timer;
	spin_lock_irq(&current->sighand->siglock);
	ret = hrtimer_try_to_cancel(timer);
	spin_unlock_irq(&current->sighand->siglock);
	KUNIT_ASSERT_GE(test, ret, 0);
	KUNIT_EXPECT_FALSE(test, hrtimer_is_queued(timer));

	itimer_test_rearm();
	KUNIT_EXPECT_TRUE(test, hrtimer_is_queued(timer));

	KUNIT_ASSERT_EQ(test, itimer_test_set(ITIMER_REAL, &zero, NULL), 0);
	KUNIT_ASSERT_EQ(test, itimer_test_set(ITIMER_REAL, &state->saved[ITIMER_REAL], NULL), 0);
}

static void itimer_test_alarm_rounding(struct kunit *test)
{
	const struct {
		struct timespec64 value;
		unsigned int expected;
	} cases[] = {
		{ { .tv_nsec = 400 * NSEC_PER_MSEC }, 1 },
		{ { .tv_sec = 2, .tv_nsec = 400 * NSEC_PER_MSEC }, 2 },
		{ { .tv_sec = 2, .tv_nsec = 600 * NSEC_PER_MSEC }, 3 },
	};
	struct itimer_test_state *state;
	struct itimerspec64 got = {};
	int i;

	state = itimer_test_prepare(test);
	for (i = 0; i < ARRAY_SIZE(cases); i++) {
		struct itimerspec64 value = {
			.it_value = cases[i].value,
		};

		KUNIT_ASSERT_EQ(test,
				itimer_test_set(ITIMER_REAL, &value, NULL), 0);
		KUNIT_EXPECT_EQ(test, itimer_test_alarm(0),
				cases[i].expected);
		KUNIT_ASSERT_EQ(test, itimer_test_get(ITIMER_REAL, &got), 0);
		KUNIT_EXPECT_TRUE(test, itimer_test_spec_zero(&got));
	}

	KUNIT_ASSERT_EQ(test, itimer_test_set(ITIMER_REAL, &state->saved[ITIMER_REAL], NULL), 0);
}

static struct kunit_case itimer_test_cases[] = {
	KUNIT_CASE(itimer_test_invalid_clock),
	KUNIT_CASE(itimer_test_real_lifecycle),
	KUNIT_CASE(itimer_test_cpu_lifecycle),
	KUNIT_CASE(itimer_test_repeated_replacement),
	KUNIT_CASE(itimer_test_periodic_rearm),
	KUNIT_CASE(itimer_test_alarm_rounding),
	{}
};

static struct kunit_suite itimer_test_suite = {
	.name = "itimer",
	.test_cases = itimer_test_cases,
};

kunit_test_suite(itimer_test_suite);

MODULE_DESCRIPTION("KUnit tests for interval timers");
MODULE_LICENSE("GPL");
