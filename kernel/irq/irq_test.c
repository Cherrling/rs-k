// SPDX-License-Identifier: LGPL-2.1+

#include <linux/cleanup.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdesc.h>
#include <linux/irqdomain.h>
#include <linux/nodemask.h>
#include <kunit/test.h>

#include "internals.h"

static irqreturn_t noop_handler(int irq, void *data)
{
	return IRQ_HANDLED;
}

static void noop(struct irq_data *data) { }
static unsigned int noop_ret(struct irq_data *data) { return 0; }

static int noop_affinity(struct irq_data *data, const struct cpumask *dest,
			 bool force)
{
	irq_data_update_effective_affinity(data, dest);

	return 0;
}

static struct irq_chip fake_irq_chip = {
	.name           = "fake",
	.irq_startup    = noop_ret,
	.irq_shutdown   = noop,
	.irq_enable     = noop,
	.irq_disable    = noop,
	.irq_ack        = noop,
	.irq_mask       = noop,
	.irq_unmask     = noop,
	.irq_set_affinity = noop_affinity,
	.flags          = IRQCHIP_SKIP_SET_WAKE,
};

static int irq_test_alloc_irq(struct kunit *test, struct irq_affinity_desc *affd)
{
	struct irq_desc *desc;
	int virq;

	virq = irq_domain_alloc_descs(-1, 1, 0, NUMA_NO_NODE, affd);
	KUNIT_ASSERT_GE(test, virq, 0);

	desc = irq_to_desc(virq);
	KUNIT_ASSERT_PTR_NE(test, desc, NULL);

	/* On some architectures, IRQs are NOREQUEST | NOPROBE by default. */
	irq_settings_clr_norequest(desc);

	return virq;
}

static int irq_test_setup_fake_irq(struct kunit *test, struct irq_affinity_desc *affd)
{
	int virq;

	virq = irq_test_alloc_irq(test, affd);
	KUNIT_ASSERT_GE(test, virq, 0);
	irq_set_chip_and_handler(virq, &fake_irq_chip, handle_simple_irq);
	return virq;
}

#ifdef CONFIG_HARDIRQS_SW_RESEND

struct irq_resend_test_context {
	unsigned int order[4];
	unsigned int order_count;
	unsigned int retrigger_calls;
	unsigned int handled;
	bool handler_irqs_disabled;
	int retrigger_result;
};

static void irq_resend_record_handler(struct irq_desc *desc)
{
	struct irq_resend_test_context *ctx = irq_desc_get_handler_data(desc);

	if (!ctx)
		return;

	ctx->handled++;
	ctx->handler_irqs_disabled = irqs_disabled();
	if (ctx->order_count < ARRAY_SIZE(ctx->order))
		ctx->order[ctx->order_count++] = irq_desc_get_irq(desc);
}

static int irq_resend_record_retrigger(struct irq_data *data)
{
	struct irq_resend_test_context *ctx = data->chip_data;

	ctx->retrigger_calls++;
	return ctx->retrigger_result;
}

static const struct irq_chip retriggering_fake_irq_chip = {
	.name		= "fake-retrigger",
	.irq_retrigger	= irq_resend_record_retrigger,
};

static int irq_test_setup_resend_irq(struct kunit *test,
				     const struct irq_chip *chip,
				     struct irq_resend_test_context *ctx)
{
	int virq;

	virq = irq_test_alloc_irq(test, NULL);
	KUNIT_ASSERT_GE(test, virq, 0);

	irq_set_chip_and_handler(virq, chip, irq_resend_record_handler);
	KUNIT_ASSERT_EQ(test, irq_set_chip_data(virq, ctx), 0);
	KUNIT_ASSERT_EQ(test, irq_set_handler_data(virq, ctx), 0);
	return virq;
}

static int irq_resend_check_locked(struct irq_desc *desc, bool inject)
{
	int ret;

	scoped_guard(raw_spinlock_irqsave, &desc->lock) {
		ret = check_irq_resend(desc, inject);
	}
	return ret;
}

static void irq_resend_test_begin(void)
{
	irq_resend_test_pause();
}

static void irq_resend_test_end(void)
{
	irq_resend_test_drain();
	irq_resend_test_resume();
}

#endif /* CONFIG_HARDIRQS_SW_RESEND */

static void irq_disable_depth_test(struct kunit *test)
{
	struct irq_desc *desc;
	int virq, ret;

	virq = irq_test_setup_fake_irq(test, NULL);

	desc = irq_to_desc(virq);
	KUNIT_ASSERT_PTR_NE(test, desc, NULL);

	ret = request_irq(virq, noop_handler, 0, "test_irq", NULL);
	KUNIT_ASSERT_EQ(test, ret, 0);

	KUNIT_EXPECT_EQ(test, desc->depth, 0);

	disable_irq(virq);
	KUNIT_EXPECT_EQ(test, desc->depth, 1);

	enable_irq(virq);
	KUNIT_EXPECT_EQ(test, desc->depth, 0);

	free_irq(virq, NULL);
}

static void irq_free_disabled_test(struct kunit *test)
{
	struct irq_desc *desc;
	int virq, ret;

	virq = irq_test_setup_fake_irq(test, NULL);

	desc = irq_to_desc(virq);
	KUNIT_ASSERT_PTR_NE(test, desc, NULL);

	ret = request_irq(virq, noop_handler, 0, "test_irq", NULL);
	KUNIT_ASSERT_EQ(test, ret, 0);

	KUNIT_EXPECT_EQ(test, desc->depth, 0);

	disable_irq(virq);
	KUNIT_EXPECT_EQ(test, desc->depth, 1);

	free_irq(virq, NULL);
	KUNIT_EXPECT_GE(test, desc->depth, 1);

	ret = request_irq(virq, noop_handler, 0, "test_irq", NULL);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, desc->depth, 0);

	free_irq(virq, NULL);
}

static void irq_shutdown_depth_test(struct kunit *test)
{
	struct irq_desc *desc;
	struct irq_data *data;
	int virq, ret;
	struct irq_affinity_desc affinity = {
		.is_managed = 1,
		.mask = CPU_MASK_ALL,
	};

	if (!IS_ENABLED(CONFIG_SMP))
		kunit_skip(test, "requires CONFIG_SMP for managed shutdown");

	virq = irq_test_setup_fake_irq(test, &affinity);

	desc = irq_to_desc(virq);
	KUNIT_ASSERT_PTR_NE(test, desc, NULL);

	data = irq_desc_get_irq_data(desc);
	KUNIT_ASSERT_PTR_NE(test, data, NULL);

	ret = request_irq(virq, noop_handler, 0, "test_irq", NULL);
	KUNIT_ASSERT_EQ(test, ret, 0);

	KUNIT_EXPECT_TRUE(test, irqd_is_activated(data));
	KUNIT_EXPECT_TRUE(test, irqd_is_started(data));
	KUNIT_EXPECT_TRUE(test, irqd_affinity_is_managed(data));

	KUNIT_EXPECT_EQ(test, desc->depth, 0);

	disable_irq(virq);
	KUNIT_EXPECT_EQ(test, desc->depth, 1);

	scoped_guard(raw_spinlock_irqsave, &desc->lock)
		irq_shutdown_and_deactivate(desc);

	KUNIT_EXPECT_FALSE(test, irqd_is_activated(data));
	KUNIT_EXPECT_FALSE(test, irqd_is_started(data));

	KUNIT_EXPECT_EQ(test, irq_activate(desc), 0);
#ifdef CONFIG_SMP
	irq_startup_managed(desc);
#endif

	KUNIT_EXPECT_EQ(test, desc->depth, 1);

	enable_irq(virq);
	KUNIT_EXPECT_EQ(test, desc->depth, 0);

	free_irq(virq, NULL);
}

static void irq_cpuhotplug_test(struct kunit *test)
{
	struct irq_desc *desc;
	struct irq_data *data;
	int virq, ret;
	struct irq_affinity_desc affinity = {
		.is_managed = 1,
	};

	if (!IS_ENABLED(CONFIG_SMP))
		kunit_skip(test, "requires CONFIG_SMP for CPU hotplug");
	if (!get_cpu_device(1))
		kunit_skip(test, "requires more than 1 CPU for CPU hotplug");
	if (!cpu_is_hotpluggable(1))
		kunit_skip(test, "CPU 1 must be hotpluggable");
	if (!cpu_online(1))
		kunit_skip(test, "CPU 1 must be online");

	cpumask_copy(&affinity.mask, cpumask_of(1));

	virq = irq_test_setup_fake_irq(test, &affinity);

	desc = irq_to_desc(virq);
	KUNIT_ASSERT_PTR_NE(test, desc, NULL);

	data = irq_desc_get_irq_data(desc);
	KUNIT_ASSERT_PTR_NE(test, data, NULL);

	ret = request_irq(virq, noop_handler, 0, "test_irq", NULL);
	KUNIT_ASSERT_EQ(test, ret, 0);

	KUNIT_EXPECT_TRUE(test, irqd_is_activated(data));
	KUNIT_EXPECT_TRUE(test, irqd_is_started(data));
	KUNIT_EXPECT_TRUE(test, irqd_affinity_is_managed(data));

	KUNIT_EXPECT_EQ(test, desc->depth, 0);

	disable_irq(virq);
	KUNIT_EXPECT_EQ(test, desc->depth, 1);

	KUNIT_EXPECT_EQ(test, remove_cpu(1), 0);
	KUNIT_EXPECT_GE(test, desc->depth, 1);
	KUNIT_EXPECT_EQ(test, add_cpu(1), 0);

	KUNIT_EXPECT_EQ(test, desc->depth, 1);

	enable_irq(virq);
	KUNIT_EXPECT_TRUE(test, irqd_is_activated(data));
	KUNIT_EXPECT_TRUE(test, irqd_is_started(data));
	KUNIT_EXPECT_EQ(test, desc->depth, 0);

	free_irq(virq, NULL);
}

#ifdef CONFIG_HARDIRQS_SW_RESEND

static void irq_resend_level_test(struct kunit *test)
{
	struct irq_desc *desc;
	int virq, ret;

	virq = irq_test_setup_fake_irq(test, NULL);
	desc = irq_to_desc(virq);
	KUNIT_ASSERT_PTR_NE(test, desc, NULL);

	irq_resend_test_begin();
	irq_settings_set_level(desc);
	desc->istate |= IRQS_PENDING | IRQS_WAITING;

	ret = irq_resend_check_locked(desc, false);

	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	KUNIT_EXPECT_FALSE(test, desc->istate & IRQS_PENDING);
	KUNIT_EXPECT_TRUE(test, desc->istate & IRQS_WAITING);

	irq_resend_test_end();
	irq_free_desc(virq);
}

static void irq_resend_replay_test(struct kunit *test)
{
	struct irq_desc *desc;
	int virq, ret;

	virq = irq_test_setup_fake_irq(test, NULL);
	desc = irq_to_desc(virq);
	KUNIT_ASSERT_PTR_NE(test, desc, NULL);

	irq_resend_test_begin();
	desc->istate |= IRQS_PENDING | IRQS_REPLAY;

	ret = irq_resend_check_locked(desc, false);

	KUNIT_EXPECT_EQ(test, ret, -EBUSY);
	KUNIT_EXPECT_TRUE(test, desc->istate & IRQS_PENDING);
	KUNIT_EXPECT_TRUE(test, desc->istate & IRQS_REPLAY);

	irq_resend_test_end();
	irq_free_desc(virq);
}

static void irq_resend_no_pending_test(struct kunit *test)
{
	struct irq_desc *desc;
	int virq, ret;

	virq = irq_test_setup_fake_irq(test, NULL);
	desc = irq_to_desc(virq);
	KUNIT_ASSERT_PTR_NE(test, desc, NULL);

	irq_resend_test_begin();
	ret = irq_resend_check_locked(desc, false);

	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_FALSE(test, desc->istate & IRQS_REPLAY);

	irq_resend_test_end();
	irq_free_desc(virq);
}

static void irq_resend_hw_retrigger_test(struct kunit *test)
{
	struct irq_resend_test_context ctx = {
		.retrigger_result = 1,
	};
	struct irq_desc *desc;
	int virq, ret;

	virq = irq_test_setup_resend_irq(test, &retriggering_fake_irq_chip, &ctx);
	desc = irq_to_desc(virq);
	KUNIT_ASSERT_PTR_NE(test, desc, NULL);

	irq_resend_test_begin();
	desc->istate |= IRQS_PENDING;

	ret = irq_resend_check_locked(desc, false);

	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, ctx.retrigger_calls, 1);
	KUNIT_EXPECT_FALSE(test, desc->istate & IRQS_PENDING);
	KUNIT_EXPECT_TRUE(test, desc->istate & IRQS_REPLAY);
	KUNIT_EXPECT_EQ(test, ctx.handled, 0);

	irq_resend_test_end();
	irq_free_desc(virq);
}

#ifdef CONFIG_IRQ_DOMAIN_HIERARCHY
static void irq_resend_hierarchy_retrigger_test(struct kunit *test)
{
	struct irq_resend_test_context child_ctx = {};
	struct irq_resend_test_context parent_ctx = {
		.retrigger_result = 1,
	};
	struct irq_desc *child, *parent;
	int child_irq, parent_irq, ret;

	parent_irq = irq_test_setup_resend_irq(test, &retriggering_fake_irq_chip,
					       &parent_ctx);
	parent = irq_to_desc(parent_irq);
	KUNIT_ASSERT_PTR_NE(test, parent, NULL);

	child_irq = irq_test_setup_resend_irq(test, &fake_irq_chip, &child_ctx);
	child = irq_to_desc(child_irq);
	KUNIT_ASSERT_PTR_NE(test, child, NULL);

	irq_resend_test_begin();
	child->irq_data.parent_data = &parent->irq_data;
	child->istate |= IRQS_PENDING;

	ret = irq_resend_check_locked(child, false);

	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, parent_ctx.retrigger_calls, 1);
	KUNIT_EXPECT_FALSE(test, child->istate & IRQS_PENDING);
	KUNIT_EXPECT_TRUE(test, child->istate & IRQS_REPLAY);
	KUNIT_EXPECT_EQ(test, child_ctx.handled, 0);
	KUNIT_EXPECT_EQ(test, parent_ctx.handled, 0);

	irq_resend_test_end();
	child->irq_data.parent_data = NULL;
	irq_free_desc(child_irq);
	irq_free_desc(parent_irq);
}
#endif /* CONFIG_IRQ_DOMAIN_HIERARCHY */

static void irq_resend_sw_fallback_test(struct kunit *test)
{
	struct irq_resend_test_context ctx = {
		.retrigger_result = 0,
	};
	struct irq_desc *desc;
	int virq, ret;

	virq = irq_test_setup_resend_irq(test, &retriggering_fake_irq_chip, &ctx);
	desc = irq_to_desc(virq);
	KUNIT_ASSERT_PTR_NE(test, desc, NULL);

	irq_resend_test_begin();
	desc->istate |= IRQS_PENDING;

	ret = irq_resend_check_locked(desc, false);

	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, ctx.retrigger_calls, 1);
	KUNIT_EXPECT_TRUE(test, desc->istate & IRQS_REPLAY);
	KUNIT_EXPECT_EQ(test, ctx.handled, 0);

	irq_resend_test_drain();
	KUNIT_EXPECT_EQ(test, ctx.handled, 1);
	KUNIT_EXPECT_TRUE(test, ctx.handler_irqs_disabled);

	irq_resend_test_resume();
	irq_free_desc(virq);
}

static void irq_resend_nested_dedup_test(struct kunit *test)
{
	struct irq_resend_test_context ctx = {};
	struct irq_desc *parent, *child0, *child1;
	int parent_irq, child0_irq, child1_irq;
	int ret0, ret1;

	parent_irq = irq_test_setup_resend_irq(test, &fake_irq_chip, &ctx);
	parent = irq_to_desc(parent_irq);
	KUNIT_ASSERT_PTR_NE(test, parent, NULL);

	child0_irq = irq_test_setup_fake_irq(test, NULL);
	child0 = irq_to_desc(child0_irq);
	KUNIT_ASSERT_PTR_NE(test, child0, NULL);

	child1_irq = irq_test_setup_fake_irq(test, NULL);
	child1 = irq_to_desc(child1_irq);
	KUNIT_ASSERT_PTR_NE(test, child1, NULL);

	irq_resend_test_begin();

	child0->status_use_accessors |= _IRQ_NESTED_THREAD;
	child0->parent_irq = parent_irq;
	child0->istate |= IRQS_PENDING;
	child1->status_use_accessors |= _IRQ_NESTED_THREAD;
	child1->parent_irq = parent_irq;
	child1->istate |= IRQS_PENDING;

	ret0 = irq_resend_check_locked(child0, false);
	ret1 = irq_resend_check_locked(child1, false);

	KUNIT_EXPECT_EQ(test, ret0, 0);
	KUNIT_EXPECT_EQ(test, ret1, 0);
	KUNIT_EXPECT_TRUE(test, child0->istate & IRQS_REPLAY);
	KUNIT_EXPECT_TRUE(test, child1->istate & IRQS_REPLAY);
	KUNIT_EXPECT_EQ(test, ctx.handled, 0);

	irq_resend_test_drain();
	KUNIT_EXPECT_EQ(test, ctx.handled, 1);
	KUNIT_EXPECT_TRUE(test, ctx.handler_irqs_disabled);

	irq_resend_test_resume();
	irq_free_desc(child1_irq);
	irq_free_desc(child0_irq);
	irq_free_desc(parent_irq);
}

static void irq_resend_drain_order_test(struct kunit *test)
{
	struct irq_resend_test_context ctx = {};
	struct irq_desc *desc0, *desc1;
	int virq0, virq1;
	int ret0, ret1;

	virq0 = irq_test_setup_resend_irq(test, &fake_irq_chip, &ctx);
	desc0 = irq_to_desc(virq0);
	KUNIT_ASSERT_PTR_NE(test, desc0, NULL);

	virq1 = irq_test_setup_resend_irq(test, &fake_irq_chip, &ctx);
	desc1 = irq_to_desc(virq1);
	KUNIT_ASSERT_PTR_NE(test, desc1, NULL);

	irq_resend_test_begin();
	desc0->istate |= IRQS_PENDING;
	desc1->istate |= IRQS_PENDING;

	ret0 = irq_resend_check_locked(desc0, false);
	ret1 = irq_resend_check_locked(desc1, false);

	KUNIT_EXPECT_EQ(test, ret0, 0);
	KUNIT_EXPECT_EQ(test, ret1, 0);

	irq_resend_test_drain();

	KUNIT_EXPECT_EQ(test, ctx.order_count, 2);
	KUNIT_EXPECT_EQ(test, ctx.order[0], virq1);
	KUNIT_EXPECT_EQ(test, ctx.order[1], virq0);
	KUNIT_EXPECT_TRUE(test, ctx.handler_irqs_disabled);

	irq_resend_test_resume();
	irq_free_desc(virq1);
	irq_free_desc(virq0);
}

static void irq_resend_reject_test(struct kunit *test)
{
	struct irq_desc *desc;
	int virq, ret;

	virq = irq_test_setup_fake_irq(test, NULL);
	desc = irq_to_desc(virq);
	KUNIT_ASSERT_PTR_NE(test, desc, NULL);

	irq_resend_test_begin();
	desc->istate |= IRQS_PENDING;
	irqd_set_handle_enforce_irqctx(&desc->irq_data);
	ret = irq_resend_check_locked(desc, false);

	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	KUNIT_EXPECT_FALSE(test, desc->istate & IRQS_REPLAY);

	irqd_clear(&desc->irq_data, IRQD_HANDLE_ENFORCE_IRQCTX);
	desc->status_use_accessors |= _IRQ_NESTED_THREAD;
	desc->istate |= IRQS_PENDING;
	ret = irq_resend_check_locked(desc, false);

	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	KUNIT_EXPECT_FALSE(test, desc->istate & IRQS_REPLAY);

	irq_resend_test_end();
	irq_free_desc(virq);
}

#ifdef CONFIG_GENERIC_IRQ_INJECTION
static void irq_resend_injection_test(struct kunit *test)
{
	struct irq_resend_test_context ctx = {};
	struct irq_desc *desc;
	int virq, ret;

	virq = irq_test_setup_resend_irq(test, &fake_irq_chip, &ctx);
	desc = irq_to_desc(virq);
	KUNIT_ASSERT_PTR_NE(test, desc, NULL);

	irq_resend_test_begin();
	irqd_set_activated(&desc->irq_data);

	ret = irq_inject_interrupt(virq);

	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_TRUE(test, desc->istate & IRQS_REPLAY);
	KUNIT_EXPECT_EQ(test, ctx.handled, 0);

	irq_resend_test_drain();
	KUNIT_EXPECT_EQ(test, ctx.handled, 1);
	KUNIT_EXPECT_TRUE(test, ctx.handler_irqs_disabled);

	irq_resend_test_resume();
	irq_free_desc(virq);
}
#endif /* CONFIG_GENERIC_IRQ_INJECTION */

#endif /* CONFIG_HARDIRQS_SW_RESEND */

static struct kunit_case irq_test_cases[] = {
	KUNIT_CASE(irq_disable_depth_test),
	KUNIT_CASE(irq_free_disabled_test),
	KUNIT_CASE(irq_shutdown_depth_test),
	KUNIT_CASE(irq_cpuhotplug_test),
#ifdef CONFIG_HARDIRQS_SW_RESEND
	KUNIT_CASE(irq_resend_level_test),
	KUNIT_CASE(irq_resend_replay_test),
	KUNIT_CASE(irq_resend_no_pending_test),
	KUNIT_CASE(irq_resend_hw_retrigger_test),
#ifdef CONFIG_IRQ_DOMAIN_HIERARCHY
	KUNIT_CASE(irq_resend_hierarchy_retrigger_test),
#endif
	KUNIT_CASE(irq_resend_sw_fallback_test),
	KUNIT_CASE(irq_resend_nested_dedup_test),
	KUNIT_CASE(irq_resend_drain_order_test),
	KUNIT_CASE(irq_resend_reject_test),
#ifdef CONFIG_GENERIC_IRQ_INJECTION
	KUNIT_CASE(irq_resend_injection_test),
#endif
#endif
	{}
};

static struct kunit_suite irq_test_suite = {
	.name = "irq_test_cases",
	.test_cases = irq_test_cases,
};

kunit_test_suite(irq_test_suite);
MODULE_DESCRIPTION("IRQ unit test suite");
MODULE_LICENSE("GPL");
