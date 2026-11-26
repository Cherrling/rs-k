// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>

#include <linux/device/bus.h>
#include <linux/device/driver.h>
#include <linux/device.h>

static const struct bus_type driver_test_bus = {
	.name = "driver-kunit",
};

static struct device_driver driver_test_register_driver = {
	.name = "driver-kunit-register",
	.bus = &driver_test_bus,
};

static struct device_driver driver_test_duplicate_first = {
	.name = "driver-kunit-duplicate",
	.bus = &driver_test_bus,
};

static struct device_driver driver_test_duplicate_second = {
	.name = "driver-kunit-duplicate",
	.bus = &driver_test_bus,
};

static struct attribute driver_test_failure_attr = {
	.name = "uevent",
	.mode = 0400,
};

static struct attribute *driver_test_failure_attrs[] = {
	&driver_test_failure_attr,
	NULL,
};

static const struct attribute_group driver_test_failure_group = {
	.attrs = driver_test_failure_attrs,
};

static const struct attribute_group *driver_test_failure_groups[] = {
	&driver_test_failure_group,
	NULL,
};

static struct device_driver driver_test_rollback_driver = {
	.name = "driver-kunit-rollback",
	.bus = &driver_test_bus,
	.groups = driver_test_failure_groups,
};

static struct device_driver driver_test_recovery_driver = {
	.name = "driver-kunit-rollback",
	.bus = &driver_test_bus,
};

static struct attribute driver_test_marker_attr = {
	.name = "driver-kunit-marker",
	.mode = 0400,
};

static struct attribute *driver_test_marker_attrs[] = {
	&driver_test_marker_attr,
	NULL,
};

static const struct attribute_group driver_test_marker_group = {
	.attrs = driver_test_marker_attrs,
};

static const struct attribute_group *driver_test_marker_groups[] = {
	&driver_test_marker_group,
	NULL,
};

static struct device_driver driver_test_marker_first = {
	.name = "driver-kunit-groups",
	.bus = &driver_test_bus,
	.groups = driver_test_marker_groups,
};

static struct device_driver driver_test_marker_second = {
	.name = "driver-kunit-groups",
	.bus = &driver_test_bus,
	.groups = driver_test_marker_groups,
};

static struct device_driver driver_test_unregistered_bus_driver = {
	.name = "driver-kunit-unregistered-bus",
};

static const struct bus_type driver_test_unregistered_bus = {
	.name = "driver-kunit-unregistered-bus",
};

static void driver_unregister_action(void *data)
{
	driver_unregister(data);
}

static int driver_test_suite_init(struct kunit_suite *suite)
{
	return bus_register(&driver_test_bus);
}

static void driver_test_suite_exit(struct kunit_suite *suite)
{
	bus_unregister(&driver_test_bus);
}

static void driver_test_register_unregister(struct kunit *test)
{
	struct device_driver *drv = &driver_test_register_driver;

	KUNIT_ASSERT_EQ(test, 0, driver_register(drv));
	KUNIT_ASSERT_EQ(test, 0,
			kunit_add_action_or_reset(test, driver_unregister_action,
						  drv));
	KUNIT_EXPECT_PTR_EQ(test, drv,
			    driver_find(drv->name, drv->bus));

	driver_unregister(drv);
	KUNIT_EXPECT_NULL(test, driver_find(drv->name, drv->bus));
	kunit_remove_action(test, driver_unregister_action, drv);
}

static void driver_test_duplicate_register(struct kunit *test)
{
	struct device_driver *first = &driver_test_duplicate_first;
	struct device_driver *second = &driver_test_duplicate_second;

	KUNIT_ASSERT_EQ(test, 0, driver_register(first));
	KUNIT_ASSERT_EQ(test, 0,
			kunit_add_action_or_reset(test, driver_unregister_action,
						  first));
	KUNIT_EXPECT_EQ(test, -EBUSY, driver_register(second));
	KUNIT_EXPECT_PTR_EQ(test, first,
			    driver_find(first->name, first->bus));

	driver_unregister(first);
	KUNIT_EXPECT_NULL(test, driver_find(first->name, first->bus));
	kunit_remove_action(test, driver_unregister_action, first);
}

static void driver_test_group_failure_rollback(struct kunit *test)
{
	struct device_driver *drv = &driver_test_rollback_driver;
	struct device_driver *recovery = &driver_test_recovery_driver;
	int error;

	error = driver_register(drv);
	KUNIT_EXPECT_EQ(test, -EEXIST, error);
	KUNIT_EXPECT_NULL(test, driver_find(drv->name, drv->bus));

	KUNIT_ASSERT_EQ(test, 0, driver_register(recovery));
	KUNIT_ASSERT_EQ(test, 0,
			kunit_add_action_or_reset(test, driver_unregister_action,
						  recovery));
	KUNIT_EXPECT_PTR_EQ(test, recovery,
			    driver_find(recovery->name, recovery->bus));

	driver_unregister(recovery);
	KUNIT_EXPECT_NULL(test, driver_find(recovery->name, recovery->bus));
	kunit_remove_action(test, driver_unregister_action, recovery);
}

static void driver_test_group_cleanup_reuse(struct kunit *test)
{
	struct device_driver *first = &driver_test_marker_first;
	struct device_driver *second = &driver_test_marker_second;

	KUNIT_ASSERT_EQ(test, 0, driver_register(first));
	driver_unregister(first);

	KUNIT_ASSERT_EQ(test, 0, driver_register(second));
	KUNIT_ASSERT_EQ(test, 0,
			kunit_add_action_or_reset(test, driver_unregister_action,
						  second));
	KUNIT_EXPECT_PTR_EQ(test, second,
			    driver_find(second->name, second->bus));

	driver_unregister(second);
	KUNIT_EXPECT_NULL(test, driver_find(second->name, second->bus));
	kunit_remove_action(test, driver_unregister_action, second);
}

static void driver_test_reject_unregistered_bus(struct kunit *test)
{
	struct device_driver *drv = &driver_test_unregistered_bus_driver;

	drv->bus = &driver_test_unregistered_bus;
	KUNIT_EXPECT_EQ(test, -EINVAL, driver_register(drv));
}

static struct kunit_case driver_test_cases[] = {
	KUNIT_CASE(driver_test_register_unregister),
	KUNIT_CASE(driver_test_duplicate_register),
	KUNIT_CASE(driver_test_group_failure_rollback),
	KUNIT_CASE(driver_test_group_cleanup_reuse),
	KUNIT_CASE(driver_test_reject_unregistered_bus),
	{}
};

static struct kunit_suite driver_test_suite = {
	.name = "driver-lifecycle",
	.suite_init = driver_test_suite_init,
	.suite_exit = driver_test_suite_exit,
	.test_cases = driver_test_cases,
};

kunit_test_suite(driver_test_suite);

MODULE_DESCRIPTION("KUnit tests for the device driver lifecycle");
MODULE_LICENSE("GPL");
