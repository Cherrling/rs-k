// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>

#include <linux/device/bus.h>
#include <linux/device/class.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/slab.h>

struct device_test_device {
	struct device dev;
	bool *released;
};

static const struct bus_type device_test_bus = {
	.name = "device-kunit-bus",
};

static struct class device_test_class = {
	.name = "device-kunit-class",
};

static struct attribute device_test_failure_attr = {
	.name = "uevent",
	.mode = 0400,
};

static struct attribute *device_test_failure_attrs[] = {
	&device_test_failure_attr,
	NULL,
};

static const struct attribute_group device_test_failure_group = {
	.attrs = device_test_failure_attrs,
};

static const struct attribute_group *device_test_failure_groups[] = {
	&device_test_failure_group,
	NULL,
};

static const struct bus_type device_test_failure_bus = {
	.name = "device-kunit-failure-bus",
	.dev_groups = device_test_failure_groups,
};

static void device_test_release(struct device *dev)
{
	struct device_test_device *test_dev =
		container_of(dev, struct device_test_device, dev);

	if (test_dev->released)
		*test_dev->released = true;
	kfree(test_dev);
}

static struct device_test_device *device_test_alloc(const char *name,
						    const struct bus_type *bus,
						    struct class *class,
						    bool *released)
{
	struct device_test_device *test_dev;

	test_dev = kzalloc_obj(*test_dev);
	if (!test_dev)
		return NULL;

	test_dev->released = released;
	test_dev->dev.release = device_test_release;
	test_dev->dev.bus = bus;
	test_dev->dev.class = class;
	test_dev->dev.init_name = name;
	return test_dev;
}

static void device_unregister_action(void *data)
{
	device_unregister(data);
}

static int device_test_suite_init(struct kunit_suite *suite)
{
	int error;

	error = bus_register(&device_test_bus);
	if (error)
		return error;

	error = bus_register(&device_test_failure_bus);
	if (error)
		return error;

	return class_register(&device_test_class);
}

static void device_test_suite_exit(struct kunit_suite *suite)
{
	class_unregister(&device_test_class);
	bus_unregister(&device_test_failure_bus);
	bus_unregister(&device_test_bus);
}

static void device_test_initialize_add_delete(struct kunit *test)
{
	struct device_test_device *test_dev;
	struct device *dev;
	bool released = false;

	test_dev = device_test_alloc("device-kunit-split", &device_test_bus,
				     NULL, &released);
	KUNIT_ASSERT_NOT_NULL(test, test_dev);
	dev = &test_dev->dev;

	device_initialize(dev);
	KUNIT_ASSERT_EQ(test, 0, device_add(dev));
	KUNIT_ASSERT_EQ(test, 0,
			kunit_add_action_or_reset(test, device_unregister_action,
						  dev));

	device_del(dev);
	put_device(dev);
	KUNIT_EXPECT_TRUE(test, released);
	kunit_remove_action(test, device_unregister_action, dev);
}

static void device_test_register_unregister(struct kunit *test)
{
	struct device_test_device *test_dev;
	struct device *dev;
	bool released = false;

	test_dev = device_test_alloc("device-kunit-register", NULL,
				     &device_test_class, &released);
	KUNIT_ASSERT_NOT_NULL(test, test_dev);
	dev = &test_dev->dev;

	KUNIT_ASSERT_EQ(test, 0, device_register(dev));
	KUNIT_ASSERT_EQ(test, 0,
			kunit_add_action_or_reset(test, device_unregister_action,
						  dev));

	device_unregister(dev);
	KUNIT_EXPECT_TRUE(test, released);
	kunit_remove_action(test, device_unregister_action, dev);
}

static void device_test_duplicate_register(struct kunit *test)
{
	struct device_test_device *first;
	struct device_test_device *second;
	bool first_released = false;
	bool second_released = false;

	first = device_test_alloc("device-kunit-duplicate", &device_test_bus,
				  NULL, &first_released);
	second = device_test_alloc("device-kunit-duplicate", NULL, NULL,
				   &second_released);
	KUNIT_ASSERT_NOT_NULL(test, first);
	KUNIT_ASSERT_NOT_NULL(test, second);

	KUNIT_ASSERT_EQ(test, 0, device_register(&first->dev));
	KUNIT_ASSERT_EQ(test, 0,
			kunit_add_action_or_reset(test, device_unregister_action,
						  &first->dev));

	KUNIT_EXPECT_EQ(test, -EEXIST, device_register(&second->dev));
	put_device(&second->dev);
	KUNIT_EXPECT_TRUE(test, second_released);

	device_unregister(&first->dev);
	KUNIT_EXPECT_TRUE(test, first_released);
	kunit_remove_action(test, device_unregister_action, &first->dev);
}

static void device_test_parent_child(struct kunit *test)
{
	struct device_test_device *parent;
	struct device_test_device *child;
	bool parent_released = false;
	bool child_released = false;

	parent = device_test_alloc("device-kunit-parent", NULL, NULL,
				   &parent_released);
	child = device_test_alloc("device-kunit-child", NULL, NULL,
				  &child_released);
	KUNIT_ASSERT_NOT_NULL(test, parent);
	KUNIT_ASSERT_NOT_NULL(test, child);
	child->dev.parent = &parent->dev;

	KUNIT_ASSERT_EQ(test, 0, device_register(&parent->dev));
	KUNIT_ASSERT_EQ(test, 0,
			kunit_add_action_or_reset(test, device_unregister_action,
						  &parent->dev));
	KUNIT_ASSERT_EQ(test, 0, device_register(&child->dev));
	KUNIT_ASSERT_EQ(test, 0,
			kunit_add_action_or_reset(test, device_unregister_action,
						  &child->dev));

	device_unregister(&child->dev);
	KUNIT_EXPECT_TRUE(test, child_released);
	device_unregister(&parent->dev);
	KUNIT_EXPECT_TRUE(test, parent_released);
	kunit_remove_action(test, device_unregister_action, &child->dev);
	kunit_remove_action(test, device_unregister_action, &parent->dev);
}

static void device_test_reject_unnamed(struct kunit *test)
{
	struct device_test_device *test_dev;
	bool released = false;

	test_dev = device_test_alloc(NULL, &device_test_bus, NULL, &released);
	KUNIT_ASSERT_NOT_NULL(test, test_dev);

	KUNIT_EXPECT_EQ(test, -EINVAL, device_register(&test_dev->dev));
	put_device(&test_dev->dev);
	KUNIT_EXPECT_TRUE(test, released);
}

static void device_test_bus_group_failure_rollback(struct kunit *test)
{
	struct device_test_device *failed;
	struct device_test_device *recovery;
	bool failed_released = false;
	bool recovery_released = false;

	failed = device_test_alloc("device-kunit-rollback",
				   &device_test_failure_bus, NULL,
				   &failed_released);
	recovery = device_test_alloc("device-kunit-rollback", NULL, NULL,
				     &recovery_released);
	KUNIT_ASSERT_NOT_NULL(test, failed);
	KUNIT_ASSERT_NOT_NULL(test, recovery);

	KUNIT_EXPECT_EQ(test, -EEXIST, device_register(&failed->dev));
	put_device(&failed->dev);
	KUNIT_EXPECT_TRUE(test, failed_released);

	KUNIT_ASSERT_EQ(test, 0, device_register(&recovery->dev));
	KUNIT_ASSERT_EQ(test, 0,
			kunit_add_action_or_reset(test, device_unregister_action,
						  &recovery->dev));

	device_unregister(&recovery->dev);
	KUNIT_EXPECT_TRUE(test, recovery_released);
	kunit_remove_action(test, device_unregister_action, &recovery->dev);
}

static void device_test_sys_entry_failure_rollback(struct kunit *test)
{
	struct device_test_device *first;
	struct device_test_device *failed;
	struct device_test_device *recovery;
	bool first_released = false;
	bool failed_released = false;
	bool recovery_released = false;

	first = device_test_alloc("device-kunit-devt", NULL, NULL,
				  &first_released);
	failed = device_test_alloc("device-kunit-devt-conflict", NULL, NULL,
				   &failed_released);
	recovery = device_test_alloc("device-kunit-devt-conflict", NULL, NULL,
				     &recovery_released);
	KUNIT_ASSERT_NOT_NULL(test, first);
	KUNIT_ASSERT_NOT_NULL(test, failed);
	KUNIT_ASSERT_NOT_NULL(test, recovery);

	first->dev.devt = MKDEV(4095, 0);
	failed->dev.devt = first->dev.devt;

	KUNIT_ASSERT_EQ(test, 0, device_register(&first->dev));
	KUNIT_ASSERT_EQ(test, 0,
			kunit_add_action_or_reset(test, device_unregister_action,
						  &first->dev));

	KUNIT_EXPECT_EQ(test, -EEXIST, device_register(&failed->dev));
	put_device(&failed->dev);
	KUNIT_EXPECT_TRUE(test, failed_released);

	KUNIT_ASSERT_EQ(test, 0, device_register(&recovery->dev));
	KUNIT_ASSERT_EQ(test, 0,
			kunit_add_action_or_reset(test, device_unregister_action,
						  &recovery->dev));

	device_unregister(&recovery->dev);
	KUNIT_EXPECT_TRUE(test, recovery_released);
	device_unregister(&first->dev);
	KUNIT_EXPECT_TRUE(test, first_released);
	kunit_remove_action(test, device_unregister_action, &recovery->dev);
	kunit_remove_action(test, device_unregister_action, &first->dev);
}

static struct kunit_case device_test_cases[] = {
	KUNIT_CASE(device_test_initialize_add_delete),
	KUNIT_CASE(device_test_register_unregister),
	KUNIT_CASE(device_test_duplicate_register),
	KUNIT_CASE(device_test_parent_child),
	KUNIT_CASE(device_test_reject_unnamed),
	KUNIT_CASE(device_test_bus_group_failure_rollback),
	KUNIT_CASE(device_test_sys_entry_failure_rollback),
	{}
};

static struct kunit_suite device_test_suite = {
	.name = "device-lifecycle",
	.suite_init = device_test_suite_init,
	.suite_exit = device_test_suite_exit,
	.test_cases = device_test_cases,
};

kunit_test_suite(device_test_suite);

MODULE_DESCRIPTION("KUnit tests for the device lifecycle");
MODULE_LICENSE("GPL");
