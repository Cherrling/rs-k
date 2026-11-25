// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>

#include <linux/device/class.h>
#include <linux/err.h>

static const struct kobj_ns_type_operations class_test_ns_ops;

static struct class class_test_register_cls = {
	.name = "class-kunit-register",
};

static struct class class_test_duplicate_first = {
	.name = "class-kunit-duplicate",
};

static struct class class_test_duplicate_second = {
	.name = "class-kunit-duplicate",
};

static const struct ns_common *class_test_namespace(const struct device *dev)
{
	return NULL;
}

static void class_unregister_action(void *data)
{
	class_unregister(data);
}

static void class_destroy_action(void *data)
{
	class_destroy(data);
}

static void class_test_namespace_validation(struct kunit *test)
{
	struct class missing_namespace = {
		.name = "class-kunit-missing-namespace",
		.ns_type = &class_test_ns_ops,
	};
	struct class missing_ns_type = {
		.name = "class-kunit-missing-ns-type",
		.namespace = class_test_namespace,
	};

	KUNIT_EXPECT_EQ(test, -EINVAL, class_register(&missing_namespace));
	KUNIT_EXPECT_FALSE(test, class_is_registered(&missing_namespace));
	KUNIT_EXPECT_EQ(test, -EINVAL, class_register(&missing_ns_type));
	KUNIT_EXPECT_FALSE(test, class_is_registered(&missing_ns_type));
	class_unregister(NULL);
}

static void class_test_register_unregister(struct kunit *test)
{
	struct class *cls = &class_test_register_cls;

	KUNIT_EXPECT_FALSE(test, class_is_registered(cls));
	KUNIT_ASSERT_EQ(test, 0, class_register(cls));
	KUNIT_ASSERT_EQ(test, 0,
			kunit_add_action_or_reset(test, class_unregister_action,
						  cls));
	KUNIT_EXPECT_TRUE(test, class_is_registered(cls));

	class_unregister(cls);
	KUNIT_EXPECT_FALSE(test, class_is_registered(cls));
	kunit_remove_action(test, class_unregister_action, cls);
	class_unregister(cls);
}

static void class_test_duplicate_register(struct kunit *test)
{
	struct class *first = &class_test_duplicate_first;
	struct class *second = &class_test_duplicate_second;

	KUNIT_ASSERT_EQ(test, 0, class_register(first));
	KUNIT_ASSERT_EQ(test, 0,
			kunit_add_action_or_reset(test, class_unregister_action,
						  first));
	KUNIT_EXPECT_EQ(test, -EEXIST, class_register(second));
	KUNIT_EXPECT_FALSE(test, class_is_registered(second));

	class_unregister(first);
	KUNIT_EXPECT_FALSE(test, class_is_registered(first));
	kunit_remove_action(test, class_unregister_action, first);
}

static void class_test_create_destroy(struct kunit *test)
{
	const char *name = "class-kunit-dynamic";
	struct class *cls;
	struct class *duplicate;
	struct class *replacement;

	cls = class_create(name);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, cls);
	KUNIT_ASSERT_EQ(test, 0,
			kunit_add_action_or_reset(test, class_destroy_action,
						  cls));
	KUNIT_EXPECT_TRUE(test, class_is_registered(cls));

	duplicate = class_create(name);
	KUNIT_EXPECT_TRUE(test, IS_ERR(duplicate));
	KUNIT_EXPECT_EQ(test, -EEXIST, PTR_ERR(duplicate));

	kunit_remove_action(test, class_destroy_action, cls);
	class_destroy(cls);
	replacement = class_create(name);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, replacement);
	KUNIT_ASSERT_EQ(test, 0,
			kunit_add_action_or_reset(test, class_destroy_action,
						  replacement));
	KUNIT_EXPECT_TRUE(test, class_is_registered(replacement));

	class_destroy(NULL);
	class_destroy(ERR_PTR(-EINVAL));
}

static struct kunit_case class_test_cases[] = {
	KUNIT_CASE(class_test_namespace_validation),
	KUNIT_CASE(class_test_register_unregister),
	KUNIT_CASE(class_test_duplicate_register),
	KUNIT_CASE(class_test_create_destroy),
	{}
};

static struct kunit_suite class_test_suite = {
	.name = "class",
	.test_cases = class_test_cases,
};

kunit_test_suite(class_test_suite);

MODULE_DESCRIPTION("KUnit tests for the device class lifecycle");
MODULE_LICENSE("GPL");
