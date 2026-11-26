// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>

#include <linux/device/bus.h>
#include <linux/device.h>

#include "../base.h"

static const struct bus_type bus_test_register_bus = {
	.name = "bus-kunit-register",
};

static const struct bus_type bus_test_duplicate_first = {
	.name = "bus-kunit-duplicate",
};

static const struct bus_type bus_test_duplicate_second = {
	.name = "bus-kunit-duplicate",
};

static struct attribute bus_test_failure_attr = {
	.name = "failure",
	.mode = 0400,
};

static struct attribute *bus_test_failure_attrs[] = {
	&bus_test_failure_attr,
	NULL,
};

static const struct attribute_group bus_test_collision_group = {
	.name = "devices",
	.attrs = bus_test_failure_attrs,
};

static const struct attribute_group *bus_test_collision_groups[] = {
	&bus_test_collision_group,
	NULL,
};

static const struct bus_type bus_test_rollback_bus = {
	.name = "bus-kunit-rollback",
	.bus_groups = bus_test_collision_groups,
};

static const struct bus_type bus_test_rollback_recovery_bus = {
	.name = "bus-kunit-rollback",
};

static void *bus_action_context(const struct bus_type *bus)
{
	return (void *)bus;
}

static void bus_unregister_action(void *data)
{
	bus_unregister(data);
}

static void bus_test_register_unregister(struct kunit *test)
{
	const struct bus_type *bus = &bus_test_register_bus;

	KUNIT_EXPECT_FALSE(test, bus_is_registered(bus));
	KUNIT_ASSERT_EQ(test, 0, bus_register(bus));
	KUNIT_ASSERT_EQ(test, 0,
			kunit_add_action_or_reset(test, bus_unregister_action,
						  bus_action_context(bus)));
	KUNIT_EXPECT_TRUE(test, bus_is_registered(bus));
	KUNIT_EXPECT_NOT_NULL(test, bus_get_kset(bus));

	bus_unregister(bus);
	KUNIT_EXPECT_FALSE(test, bus_is_registered(bus));
	kunit_remove_action(test, bus_unregister_action,
			    bus_action_context(bus));
	bus_unregister(bus);
	bus_unregister(NULL);
}

static void bus_test_duplicate_register(struct kunit *test)
{
	const struct bus_type *first = &bus_test_duplicate_first;
	const struct bus_type *second = &bus_test_duplicate_second;

	KUNIT_ASSERT_EQ(test, 0, bus_register(first));
	KUNIT_ASSERT_EQ(test, 0,
			kunit_add_action_or_reset(test, bus_unregister_action,
						  bus_action_context(first)));
	KUNIT_EXPECT_EQ(test, -EEXIST, bus_register(second));
	KUNIT_EXPECT_FALSE(test, bus_is_registered(second));

	bus_unregister(first);
	KUNIT_EXPECT_FALSE(test, bus_is_registered(first));
	kunit_remove_action(test, bus_unregister_action,
			    bus_action_context(first));
}

static void bus_test_group_failure_rollback(struct kunit *test)
{
	const struct bus_type *bus = &bus_test_rollback_bus;
	const struct bus_type *recovery = &bus_test_rollback_recovery_bus;
	int error;

	error = bus_register(bus);
	KUNIT_EXPECT_EQ(test, -EEXIST, error);
	KUNIT_EXPECT_FALSE(test, bus_is_registered(bus));

	KUNIT_ASSERT_EQ(test, 0, bus_register(recovery));
	KUNIT_ASSERT_EQ(test, 0,
			kunit_add_action_or_reset(test, bus_unregister_action,
						  bus_action_context(recovery)));
	KUNIT_EXPECT_TRUE(test, bus_is_registered(recovery));

	bus_unregister(recovery);
	KUNIT_EXPECT_FALSE(test, bus_is_registered(recovery));
	kunit_remove_action(test, bus_unregister_action,
			    bus_action_context(recovery));
}

static struct kunit_case bus_test_cases[] = {
	KUNIT_CASE(bus_test_register_unregister),
	KUNIT_CASE(bus_test_duplicate_register),
	KUNIT_CASE(bus_test_group_failure_rollback),
	{}
};

static struct kunit_suite bus_test_suite = {
	.name = "bus-lifecycle",
	.test_cases = bus_test_cases,
};

kunit_test_suite(bus_test_suite);

MODULE_DESCRIPTION("KUnit tests for the device bus lifecycle");
MODULE_LICENSE("GPL");
