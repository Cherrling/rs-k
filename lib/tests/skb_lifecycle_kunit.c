// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>
#include <linux/anon_inodes.h>
#include <linux/completion.h>
#include <linux/cred.h>
#include <linux/err.h>
#include <linux/file-lifecycle-rust.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/refcount.h>
#include <linux/sched.h>
#include <linux/skb-lifecycle-rust.h>
#include <linux/skbuff.h>
#include <net/net_namespace.h>
#include <net/sock.h>

#define SKB_LIFECYCLE_TEST_TIMEOUT		(5 * HZ)
#define SKB_LIFECYCLE_TEST_WORKERS		4
#define SKB_LIFECYCLE_TEST_ITERATIONS		64
#define SKB_LIFECYCLE_TEST_LIST_COUNT		20

struct skb_lifecycle_test_worker {
	struct completion done;
	atomic_t errors;
};

static const struct file_operations skb_lifecycle_test_fops = {
	.owner = THIS_MODULE,
};

static void skb_lifecycle_test_reset(void)
{
#ifdef CONFIG_RUST_SKB_LIFECYCLE
	rust_skb_test_reset();
#endif
}

#ifdef CONFIG_RUST_SKB_LIFECYCLE
static u32 skb_lifecycle_test_counter(unsigned int counter)
{
	return rust_helper_skb_test_counter(counter);
}
#endif

static void skb_lifecycle_test_alloc_free(struct kunit *test)
{
	struct sk_buff *skb;
#ifdef CONFIG_RUST_SKB_LIFECYCLE
	u32 free_before;
#endif

	skb_lifecycle_test_reset();
	skb = alloc_skb(256, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, skb);
	KUNIT_EXPECT_EQ(test, 0, skb->len);
	KUNIT_EXPECT_EQ(test, 1, refcount_read(&skb->users));
	KUNIT_EXPECT_GE(test, skb_tailroom(skb), 256);
#ifdef CONFIG_RUST_SKB_LIFECYCLE
	KUNIT_EXPECT_EQ(test, 1,
			skb_lifecycle_test_counter(RUST_SKB_TEST_COUNTER_ALLOC));
	free_before = skb_lifecycle_test_counter(RUST_SKB_TEST_COUNTER_FREE);
#endif

	consume_skb(skb);
#ifdef CONFIG_RUST_SKB_LIFECYCLE
	KUNIT_EXPECT_EQ(test, free_before + 1,
			skb_lifecycle_test_counter(RUST_SKB_TEST_COUNTER_FREE));
#endif
}

#ifdef CONFIG_RUST_SKB_LIFECYCLE
static void skb_lifecycle_test_failure_injection(struct kunit *test)
{
	struct sk_buff *skb;

	skb_lifecycle_test_reset();
	rust_skb_test_fail_next(RUST_SKB_TEST_FAIL_SHELL);
	skb = alloc_skb(256, GFP_KERNEL);
	KUNIT_EXPECT_NULL(test, skb);
	KUNIT_EXPECT_EQ(test, 0,
			skb_lifecycle_test_counter(RUST_SKB_TEST_COUNTER_ALLOC));
	KUNIT_EXPECT_EQ(test, 0,
			skb_lifecycle_test_counter(RUST_SKB_TEST_COUNTER_FREE));

	rust_skb_test_fail_next(RUST_SKB_TEST_FAIL_HEAD);
	skb = alloc_skb(256, GFP_KERNEL);
	KUNIT_EXPECT_NULL(test, skb);
	KUNIT_EXPECT_EQ(test, 0,
			skb_lifecycle_test_counter(RUST_SKB_TEST_COUNTER_ALLOC));
	KUNIT_EXPECT_EQ(test, 1,
			skb_lifecycle_test_counter(RUST_SKB_TEST_COUNTER_FREE));

	skb = alloc_skb(256, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, skb);
	rust_skb_test_fail_next(RUST_SKB_TEST_FAIL_CLONE);
	KUNIT_EXPECT_NULL(test, skb_clone(skb, GFP_KERNEL));
	KUNIT_EXPECT_EQ(test, 0,
			skb_lifecycle_test_counter(RUST_SKB_TEST_COUNTER_CLONE));
	consume_skb(skb);
}
#endif

static void skb_lifecycle_test_clone_shared(struct kunit *test)
{
	struct sk_buff *clone;
	struct sk_buff *skb;

	skb_lifecycle_test_reset();
	skb = alloc_skb(256, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, skb);
	skb_put_u8(skb, 0x5a);

	clone = skb_clone(skb, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, clone);
	KUNIT_EXPECT_PTR_NE(test, skb, clone);
	KUNIT_EXPECT_PTR_EQ(test, skb->data, clone->data);
	KUNIT_EXPECT_EQ(test, 1, skb->len);
	KUNIT_EXPECT_EQ(test, 1, clone->len);
	KUNIT_EXPECT_TRUE(test, skb_cloned(skb));
	KUNIT_EXPECT_TRUE(test, skb_cloned(clone));
	KUNIT_EXPECT_EQ(test, 1, refcount_read(&clone->users));
#ifdef CONFIG_RUST_SKB_LIFECYCLE
	KUNIT_EXPECT_EQ(test, 1,
			skb_lifecycle_test_counter(RUST_SKB_TEST_COUNTER_CLONE));
#endif

	consume_skb(clone);
	consume_skb(skb);
}

static void skb_lifecycle_test_fclone(struct kunit *test)
{
	struct sk_buff *clone2;
	struct sk_buff *clone;
	struct sk_buff *skb;

	skb_lifecycle_test_reset();
	skb = alloc_skb_fclone(256, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, skb);
	KUNIT_EXPECT_EQ(test, (u32)SKB_FCLONE_ORIG, (u32)skb->fclone);

	clone = skb_clone(skb, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, clone);
	KUNIT_EXPECT_EQ(test, (u32)SKB_FCLONE_CLONE, (u32)clone->fclone);
	KUNIT_EXPECT_PTR_EQ(test, skb->head, clone->head);

	clone2 = skb_clone(skb, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, clone2);
	KUNIT_EXPECT_EQ(test, (u32)SKB_FCLONE_UNAVAILABLE,
			(u32)clone2->fclone);
#ifdef CONFIG_RUST_SKB_LIFECYCLE
	KUNIT_EXPECT_EQ(test, 2,
			skb_lifecycle_test_counter(RUST_SKB_TEST_COUNTER_CLONE));
#endif

	consume_skb(clone);
	consume_skb(clone2);
	consume_skb(skb);
}

static void skb_lifecycle_test_reference_drop(struct kunit *test)
{
	struct sk_buff *skb;
#ifdef CONFIG_RUST_SKB_LIFECYCLE
	u32 free_before;
#endif

	skb_lifecycle_test_reset();
	skb = alloc_skb(128, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, skb);
	refcount_inc(&skb->users);
#ifdef CONFIG_RUST_SKB_LIFECYCLE
	free_before = skb_lifecycle_test_counter(RUST_SKB_TEST_COUNTER_FREE);
#endif

	sk_skb_reason_drop(NULL, skb, SKB_DROP_REASON_NOT_SPECIFIED);
	KUNIT_EXPECT_EQ(test, 1, refcount_read(&skb->users));
#ifdef CONFIG_RUST_SKB_LIFECYCLE
	KUNIT_EXPECT_EQ(test, free_before,
			skb_lifecycle_test_counter(RUST_SKB_TEST_COUNTER_FREE));
#endif

	sk_skb_reason_drop(NULL, skb, SKB_CONSUMED);
#ifdef CONFIG_RUST_SKB_LIFECYCLE
	KUNIT_EXPECT_EQ(test, free_before + 1,
			skb_lifecycle_test_counter(RUST_SKB_TEST_COUNTER_FREE));
#endif
}

static void skb_lifecycle_test_stateless_consume(struct kunit *test)
{
	struct sk_buff *skb;

	skb_lifecycle_test_reset();
	skb = alloc_skb(128, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, skb);
	KUNIT_EXPECT_EQ(test, 1, refcount_read(&skb->users));

	__consume_stateless_skb(skb);
#ifdef CONFIG_RUST_SKB_LIFECYCLE
	KUNIT_EXPECT_EQ(test, 1,
			skb_lifecycle_test_counter(RUST_SKB_TEST_COUNTER_FREE));
#endif
}

static void skb_lifecycle_test_list_release(struct kunit *test)
{
	struct sk_buff *head = NULL;
	struct sk_buff *tail = NULL;
	unsigned int index;

	skb_lifecycle_test_reset();
	for (index = 0; index < SKB_LIFECYCLE_TEST_LIST_COUNT; index++) {
		struct sk_buff *skb;

		skb = alloc_skb(64, GFP_KERNEL);
		KUNIT_ASSERT_NOT_NULL(test, skb);
		skb->next = NULL;
		skb->prev = NULL;
		if (!head)
			head = skb;
		else
			tail->next = skb;
		tail = skb;
	}

	kfree_skb_list_reason(head, SKB_DROP_REASON_NOT_SPECIFIED);
#ifdef CONFIG_RUST_SKB_LIFECYCLE
	KUNIT_EXPECT_EQ(test, SKB_LIFECYCLE_TEST_LIST_COUNT,
			skb_lifecycle_test_counter(RUST_SKB_TEST_COUNTER_FREE));
#endif
}

static void skb_lifecycle_test_napi_budget_zero(struct kunit *test)
{
	struct sk_buff *skb;

	skb_lifecycle_test_reset();
	skb = alloc_skb(128, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, skb);
	napi_consume_skb(skb, 0);
}

static void skb_lifecycle_test_napi_budget(struct kunit *test)
{
	struct sk_buff *skb;

	skb_lifecycle_test_reset();
	skb = alloc_skb(128, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, skb);
	refcount_inc(&skb->users);

	local_bh_disable();
	napi_consume_skb(skb, 1);
	KUNIT_EXPECT_EQ(test, 1, refcount_read(&skb->users));
	napi_consume_skb(skb, 1);
	local_bh_enable();
}

static void skb_lifecycle_test_message_and_morph(struct kunit *test)
{
	struct sk_buff *message;
	struct sk_buff *first;
	struct sk_buff *dst;
	struct sk_buff *src;

	skb_lifecycle_test_reset();
	first = alloc_skb(128, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, first);
	skb_put_zero(first, 32);
	message = alloc_skb_for_msg(first);
	KUNIT_ASSERT_NOT_NULL(test, message);
	KUNIT_EXPECT_EQ(test, first->len, message->len);
	KUNIT_EXPECT_EQ(test, first->len, message->data_len);
	KUNIT_EXPECT_EQ(test, first->truesize, message->truesize);
	KUNIT_EXPECT_PTR_EQ(test, first, skb_shinfo(message)->frag_list);
	consume_skb(message);

	src = alloc_skb(128, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, src);
	dst = alloc_skb(128, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dst);
	skb_put_u8(src, 0xa5);
	KUNIT_EXPECT_PTR_EQ(test, dst, skb_morph(dst, src));
	KUNIT_EXPECT_PTR_EQ(test, src->data, dst->data);
	KUNIT_EXPECT_EQ(test, src->len, dst->len);
	consume_skb(dst);
	consume_skb(src);
}

static void skb_lifecycle_test_socket_owned(struct kunit *test)
{
	struct socket *sock = NULL;
	struct sk_buff *skb;
	int error;

	skb_lifecycle_test_reset();
	error = sock_create_kern(&init_net, AF_INET, SOCK_DGRAM, 0, &sock);
	KUNIT_ASSERT_EQ(test, 0, error);
	KUNIT_ASSERT_NOT_NULL(test, sock);
	KUNIT_ASSERT_NOT_NULL(test, sock->sk);

	skb = alloc_skb(256, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, skb);
	skb_set_owner_w(skb, sock->sk);
	KUNIT_EXPECT_PTR_EQ(test, sock->sk, skb->sk);
	consume_skb(skb);
	sock_release(sock);
}

static void skb_lifecycle_test_cross_layer(struct kunit *test)
{
	const struct cred *cred;
	struct socket *sock = NULL;
	struct sk_buff *clone;
	struct sk_buff *skb;
	struct file *file;
	int error;

	skb_lifecycle_test_reset();
	cred = get_current_cred();
	KUNIT_ASSERT_NOT_NULL(test, cred);

	file = anon_inode_getfile("[skb-lifecycle]", &skb_lifecycle_test_fops,
				  NULL, O_RDWR);
	KUNIT_ASSERT_FALSE(test, IS_ERR(file));
	KUNIT_EXPECT_TRUE(test, file->f_mode & FMODE_READ);
	KUNIT_EXPECT_TRUE(test, file->f_mode & FMODE_WRITE);

	error = sock_create_kern(&init_net, AF_INET, SOCK_DGRAM, 0, &sock);
	KUNIT_ASSERT_EQ(test, 0, error);
	KUNIT_ASSERT_NOT_NULL(test, sock);

	skb = alloc_skb(512, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, skb);
	skb_set_owner_w(skb, sock->sk);
	clone = skb_clone(skb, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, clone);
	consume_skb(clone);
	consume_skb(skb);

	sock_release(sock);
	fput_close_sync(file);
	put_cred(cred);
}

static int skb_lifecycle_test_worker(void *data)
{
	struct skb_lifecycle_test_worker *worker = data;
	unsigned int index;

	for (index = 0; index < SKB_LIFECYCLE_TEST_ITERATIONS; index++) {
		struct sk_buff *clone;
		struct sk_buff *skb;

		if (index % 4 == 0)
			skb = alloc_skb_fclone(256, GFP_KERNEL);
		else
			skb = alloc_skb(256, GFP_KERNEL);
		if (!skb) {
			atomic_inc(&worker->errors);
			continue;
		}

		clone = skb_clone(skb, GFP_KERNEL);
		if (!clone) {
			atomic_inc(&worker->errors);
			consume_skb(skb);
			continue;
		}

		consume_skb(clone);
		consume_skb(skb);
	}

	complete(&worker->done);
	while (!kthread_should_stop())
		schedule();
	return 0;
}

static void skb_lifecycle_test_concurrent(struct kunit *test)
{
	struct skb_lifecycle_test_worker workers[SKB_LIFECYCLE_TEST_WORKERS] = {};
	struct task_struct *tasks[SKB_LIFECYCLE_TEST_WORKERS] = {};
	unsigned int index;
	bool timed_out = false;

	skb_lifecycle_test_reset();
	for (index = 0; index < SKB_LIFECYCLE_TEST_WORKERS; index++) {
		init_completion(&workers[index].done);
		tasks[index] = kthread_run(skb_lifecycle_test_worker,
					   &workers[index],
					   "skb-lifecycle-%u", index);
		KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(tasks[index]));
		get_task_struct(tasks[index]);
	}

	for (index = 0; index < SKB_LIFECYCLE_TEST_WORKERS; index++) {
		if (!wait_for_completion_timeout(&workers[index].done,
						 SKB_LIFECYCLE_TEST_TIMEOUT))
			timed_out = true;
	}
	for (index = 0; index < SKB_LIFECYCLE_TEST_WORKERS; index++) {
		KUNIT_EXPECT_EQ(test, 0, kthread_stop_put(tasks[index]));
		KUNIT_EXPECT_EQ(test, 0, atomic_read(&workers[index].errors));
	}

	KUNIT_EXPECT_FALSE(test, timed_out);
#ifdef CONFIG_RUST_SKB_LIFECYCLE
	KUNIT_EXPECT_EQ(test,
			SKB_LIFECYCLE_TEST_WORKERS * SKB_LIFECYCLE_TEST_ITERATIONS,
			skb_lifecycle_test_counter(RUST_SKB_TEST_COUNTER_ALLOC));
	KUNIT_EXPECT_EQ(test,
			SKB_LIFECYCLE_TEST_WORKERS * SKB_LIFECYCLE_TEST_ITERATIONS,
			skb_lifecycle_test_counter(RUST_SKB_TEST_COUNTER_CLONE));
	KUNIT_EXPECT_EQ(test,
			2 * SKB_LIFECYCLE_TEST_WORKERS *
			SKB_LIFECYCLE_TEST_ITERATIONS,
			skb_lifecycle_test_counter(RUST_SKB_TEST_COUNTER_FREE));
#endif
}

static struct kunit_case skb_lifecycle_test_cases[] = {
#ifdef CONFIG_RUST_SKB_LIFECYCLE
	KUNIT_CASE(skb_lifecycle_test_failure_injection),
#endif
	KUNIT_CASE(skb_lifecycle_test_alloc_free),
	KUNIT_CASE(skb_lifecycle_test_clone_shared),
	KUNIT_CASE(skb_lifecycle_test_fclone),
	KUNIT_CASE(skb_lifecycle_test_reference_drop),
	KUNIT_CASE(skb_lifecycle_test_stateless_consume),
	KUNIT_CASE(skb_lifecycle_test_list_release),
	KUNIT_CASE(skb_lifecycle_test_napi_budget_zero),
	KUNIT_CASE(skb_lifecycle_test_napi_budget),
	KUNIT_CASE(skb_lifecycle_test_message_and_morph),
	KUNIT_CASE(skb_lifecycle_test_socket_owned),
	KUNIT_CASE(skb_lifecycle_test_cross_layer),
	KUNIT_CASE(skb_lifecycle_test_concurrent),
	{}
};

static struct kunit_suite skb_lifecycle_test_suite = {
	.name = "skb_lifecycle",
	.test_cases = skb_lifecycle_test_cases,
};

kunit_test_suite(skb_lifecycle_test_suite);

MODULE_DESCRIPTION("KUnit tests for generic skb lifecycle");
MODULE_LICENSE("GPL");
