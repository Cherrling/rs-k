// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>
#include <linux/completion.h>
#include <linux/cred.h>
#include <linux/err.h>
#include <linux/fdtable.h>
#include <linux/file.h>
#include <linux/file-lifecycle-rust.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/socket.h>
#include <linux/socket-lifecycle-rust.h>
#include <linux/task_work.h>
#include <net/net_namespace.h>
#include <net/sock.h>

#define SOCKET_LIFECYCLE_TEST_TIMEOUT		(5 * HZ)
#define SOCKET_LIFECYCLE_TEST_WORKERS		4
#define SOCKET_LIFECYCLE_TEST_ITERATIONS	128

static struct proto socket_lifecycle_test_proto;
static atomic_t socket_lifecycle_test_release_count;
static atomic_t socket_lifecycle_test_destruct_count;
static struct socket *socket_lifecycle_test_release_socket;
static const struct proto_ops *socket_lifecycle_test_release_ops;

struct socket_lifecycle_test_worker {
	struct completion done;
	atomic_t errors;
};

static int socket_lifecycle_test_release(struct socket *sock)
{
	atomic_inc(&socket_lifecycle_test_release_count);
	socket_lifecycle_test_release_socket = sock;
	socket_lifecycle_test_release_ops = sock->ops;
	return 0;
}

static void socket_lifecycle_test_destruct(struct sock *sk)
{
	atomic_inc(&socket_lifecycle_test_destruct_count);
}

static const struct proto_ops socket_lifecycle_test_ops = {
	.family = PF_UNSPEC,
	.release = socket_lifecycle_test_release,
};

static void socket_lifecycle_test_reset(void)
{
	atomic_set(&socket_lifecycle_test_release_count, 0);
	atomic_set(&socket_lifecycle_test_destruct_count, 0);
	socket_lifecycle_test_release_socket = NULL;
	socket_lifecycle_test_release_ops = NULL;
#ifdef CONFIG_RUST_SOCKET_LIFECYCLE
	rust_socket_test_reset();
#endif
}

#ifdef CONFIG_RUST_SOCKET_LIFECYCLE
static u32 socket_lifecycle_test_counter(unsigned int counter)
{
	return rust_helper_socket_test_counter(counter);
}
#endif

static struct sock *socket_lifecycle_test_sk(struct kunit *test)
{
	struct sock *sk;

	sk = sk_alloc(&init_net, AF_INET, GFP_KERNEL,
		      &socket_lifecycle_test_proto, 1);
	KUNIT_ASSERT_NOT_NULL(test, sk);
	sock_init_data_uid(NULL, sk, KUIDT_INIT(0));
	sk->sk_destruct = socket_lifecycle_test_destruct;
	return sk;
}

static void socket_lifecycle_test_alloc_release(struct kunit *test)
{
	struct socket *sock;

	socket_lifecycle_test_reset();
	sock = sock_alloc();
	KUNIT_ASSERT_NOT_NULL(test, sock);
	KUNIT_EXPECT_NULL(test, sock->sk);
	KUNIT_EXPECT_NULL(test, sock->file);

	sock->ops = &socket_lifecycle_test_ops;
	sock_release(sock);
	KUNIT_EXPECT_EQ(test, 1,
			atomic_read(&socket_lifecycle_test_release_count));
	KUNIT_EXPECT_PTR_EQ(test, sock,
			    socket_lifecycle_test_release_socket);
	KUNIT_EXPECT_PTR_EQ(test, &socket_lifecycle_test_ops,
			    socket_lifecycle_test_release_ops);
}

static void socket_lifecycle_test_create_lite(struct kunit *test)
{
	struct socket *sock = NULL;
	int error;

	socket_lifecycle_test_reset();
	error = sock_create_lite(AF_INET, SOCK_DGRAM, 0, &sock);
	KUNIT_EXPECT_EQ(test, 0, error);
	KUNIT_ASSERT_NOT_NULL(test, sock);
	KUNIT_EXPECT_EQ(test, SOCK_DGRAM, sock->type);
	KUNIT_EXPECT_NULL(test, sock->ops);
	sock_release(sock);
}

static void socket_lifecycle_test_create_kern(struct kunit *test)
{
	struct socket *sock = NULL;
	int error;

	socket_lifecycle_test_reset();
	error = sock_create_kern(&init_net, AF_INET, SOCK_DGRAM, 0, &sock);
	KUNIT_EXPECT_EQ(test, 0, error);
	KUNIT_ASSERT_NOT_NULL(test, sock);
	KUNIT_ASSERT_NOT_NULL(test, sock->sk);
	KUNIT_ASSERT_NOT_NULL(test, sock->ops);
	KUNIT_EXPECT_PTR_EQ(test, sock, sock->sk->sk_socket);
	sock_release(sock);
}

static void socket_lifecycle_test_create_invalid(struct kunit *test)
{
	struct socket *sock = NULL;

	KUNIT_EXPECT_EQ(test, -EAFNOSUPPORT,
			sock_create_kern(&init_net, -1, SOCK_DGRAM, 0,
					 &sock));
	KUNIT_EXPECT_EQ(test, -EAFNOSUPPORT,
			sock_create_kern(&init_net, NPROTO, SOCK_DGRAM, 0,
					 &sock));
	KUNIT_EXPECT_EQ(test, -EINVAL,
			sock_create_kern(&init_net, AF_INET, -1, 0, &sock));
	KUNIT_EXPECT_EQ(test, -EINVAL,
			sock_create_kern(&init_net, AF_INET, SOCK_MAX, 0,
					 &sock));
	KUNIT_EXPECT_EQ(test, -EINVAL,
			__sys_socket(AF_INET, SOCK_DGRAM | 0x4000, 0));
}

static void socket_lifecycle_test_alloc_file(struct kunit *test)
{
	struct socket *sock = NULL;
	struct file *file;
	int error;

	socket_lifecycle_test_reset();
	error = sock_create_kern(&init_net, AF_INET, SOCK_DGRAM, 0, &sock);
	KUNIT_ASSERT_EQ(test, 0, error);
	KUNIT_ASSERT_NOT_NULL(test, sock);

	file = sock_alloc_file(sock, O_NONBLOCK, NULL);
	KUNIT_ASSERT_FALSE(test, IS_ERR(file));
	KUNIT_EXPECT_PTR_EQ(test, sock, file->private_data);
	KUNIT_EXPECT_PTR_EQ(test, file, sock->file);
	KUNIT_EXPECT_TRUE(test, file->f_mode & FMODE_NOWAIT);
	KUNIT_EXPECT_TRUE(test, file->f_flags & O_NONBLOCK);
	fput_close_sync(file);
}

static void socket_lifecycle_test_sys_socket(struct kunit *test)
{
	struct socket *sock;
	struct file *file;
	int flags = SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK;
	int fd;

	fd = __sys_socket(AF_INET, flags, 0);
	KUNIT_ASSERT_GE(test, fd, 0);
	file = fget(fd);
	KUNIT_ASSERT_NOT_NULL(test, file);
	KUNIT_EXPECT_TRUE(test, file->f_flags & O_NONBLOCK);
	sock = sock_from_file(file);
	KUNIT_ASSERT_NOT_NULL(test, sock);
	KUNIT_ASSERT_NOT_NULL(test, sock->sk);
	fput(file);

	KUNIT_EXPECT_EQ(test, 0, close_fd(fd));
	task_work_run();
	flush_delayed_fput();
}

static void socket_lifecycle_test_sys_socket_file(struct kunit *test)
{
	struct socket *sock;
	struct file *file;

	file = __sys_socket_file(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
	KUNIT_ASSERT_FALSE(test, IS_ERR(file));
	KUNIT_EXPECT_TRUE(test, file->f_flags & O_NONBLOCK);
	sock = sock_from_file(file);
	KUNIT_ASSERT_NOT_NULL(test, sock);
	KUNIT_ASSERT_NOT_NULL(test, sock->sk);
	KUNIT_EXPECT_PTR_EQ(test, file, sock->file);
	fput_close_sync(file);

	file = __sys_socket_file(AF_INET, SOCK_DGRAM | 0x4000, 0);
	KUNIT_EXPECT_TRUE(test, IS_ERR(file));
	KUNIT_EXPECT_EQ(test, -EINVAL, PTR_ERR(file));
}

static void socket_lifecycle_test_sock_put(struct kunit *test)
{
	struct sock *sk;

	socket_lifecycle_test_reset();
	sk = socket_lifecycle_test_sk(test);
	KUNIT_EXPECT_EQ(test, 1, refcount_read(&sk->sk_refcnt));
#ifdef CONFIG_RUST_SOCKET_LIFECYCLE
	KUNIT_EXPECT_EQ(test, 1, socket_lifecycle_test_counter(RUST_SOCKET_TEST_COUNTER_SK_ALLOC));
#endif

	sock_put(sk);
	KUNIT_EXPECT_EQ(test, 1,
			atomic_read(&socket_lifecycle_test_destruct_count));
#ifdef CONFIG_RUST_SOCKET_LIFECYCLE
	KUNIT_EXPECT_EQ(test, 1, socket_lifecycle_test_counter(RUST_SOCKET_TEST_COUNTER_SK_FREE));
#endif
}

static void socket_lifecycle_test_rcu_destruct(struct kunit *test)
{
	struct sock *sk;

	socket_lifecycle_test_reset();
	sk = socket_lifecycle_test_sk(test);
#ifdef CONFIG_RUST_SOCKET_LIFECYCLE
	rust_socket_test_set_rcu_free(sk);
#else
	sock_set_flag(sk, SOCK_RCU_FREE);
#endif

	sock_put(sk);
	KUNIT_EXPECT_EQ(test, 0,
			atomic_read(&socket_lifecycle_test_destruct_count));
#ifdef CONFIG_RUST_SOCKET_LIFECYCLE
	rust_socket_test_synchronize_rcu();
#else
	synchronize_rcu();
	rcu_barrier();
#endif
	KUNIT_EXPECT_EQ(test, 1,
			atomic_read(&socket_lifecycle_test_destruct_count));
#ifdef CONFIG_RUST_SOCKET_LIFECYCLE
	KUNIT_EXPECT_EQ(test, 1, socket_lifecycle_test_counter(RUST_SOCKET_TEST_COUNTER_RCU_FREE));
	KUNIT_EXPECT_EQ(test, 1, socket_lifecycle_test_counter(RUST_SOCKET_TEST_COUNTER_SK_FREE));
#endif
}

#ifdef CONFIG_RUST_SOCKET_LIFECYCLE
static void socket_lifecycle_test_sk_free_deferred(struct kunit *test)
{
	struct sock *sk;

	socket_lifecycle_test_reset();
	sk = socket_lifecycle_test_sk(test);
	refcount_inc(&sk->sk_wmem_alloc);
	KUNIT_EXPECT_EQ(test, 1, socket_lifecycle_test_counter(RUST_SOCKET_TEST_COUNTER_SK_ALLOC));

	sock_put(sk);
	KUNIT_EXPECT_EQ(test, 0,
			atomic_read(&socket_lifecycle_test_destruct_count));
	KUNIT_EXPECT_EQ(test, 1, rust_socket_test_wmem_count(sk));
	KUNIT_EXPECT_EQ(test, 0, socket_lifecycle_test_counter(RUST_SOCKET_TEST_COUNTER_SK_FREE));

	rust_socket_test_wmem_put_amount(sk, 1);
	KUNIT_EXPECT_EQ(test, 1,
			atomic_read(&socket_lifecycle_test_destruct_count));
	KUNIT_EXPECT_EQ(test, 1, socket_lifecycle_test_counter(RUST_SOCKET_TEST_COUNTER_SK_FREE));
}

static void socket_lifecycle_test_failure_injection(struct kunit *test)
{
	static const struct {
		unsigned int stage;
		int error;
	} stages[] = {
		{ RUST_SOCKET_TEST_FAIL_INODE, -ENFILE },
		{ RUST_SOCKET_TEST_FAIL_SECURITY_CREATE, -EPERM },
		{ RUST_SOCKET_TEST_FAIL_FAMILY, -EAFNOSUPPORT },
		{ RUST_SOCKET_TEST_FAIL_FAMILY_MODULE, -EAFNOSUPPORT },
		{ RUST_SOCKET_TEST_FAIL_CREATE, -EAFNOSUPPORT },
		{ RUST_SOCKET_TEST_FAIL_OPS_MODULE, -EAFNOSUPPORT },
		{ RUST_SOCKET_TEST_FAIL_SECURITY_POST, -EPERM },
	};
	unsigned int index;

	socket_lifecycle_test_reset();
	for (index = 0; index < ARRAY_SIZE(stages); index++) {
		struct socket *sock = NULL;
		int error;

		rust_socket_test_fail_next(stages[index].stage);
		error = sock_create_kern(&init_net, AF_INET, SOCK_DGRAM, 0,
					 &sock);
		KUNIT_EXPECT_EQ(test, stages[index].error, error);
		if (stages[index].stage == RUST_SOCKET_TEST_FAIL_INODE)
			KUNIT_EXPECT_NULL(test, sock);
	}
	rust_socket_test_fail_next(0);

	for (index = 0; index < 3; index++) {
		static const unsigned int sk_stages[] = {
			RUST_SOCKET_TEST_FAIL_SK_ALLOC,
			RUST_SOCKET_TEST_FAIL_SK_SECURITY,
			RUST_SOCKET_TEST_FAIL_SK_MODULE,
		};
		struct sock *sk;

		rust_socket_test_fail_next(sk_stages[index]);
		sk = sk_alloc(&init_net, AF_INET, GFP_KERNEL,
			      &socket_lifecycle_test_proto, 1);
		KUNIT_EXPECT_NULL(test, sk);
	}
	rust_socket_test_fail_next(0);
}

static void socket_lifecycle_test_release_ownership(struct kunit *test)
{
	struct socket *sock = NULL;
	u32 inode_count_before;
	u32 inode_count_after;
	u32 release_count_before;
	u32 release_count_after;
	int error;

	socket_lifecycle_test_reset();
	inode_count_before = socket_lifecycle_test_counter(RUST_SOCKET_TEST_COUNTER_INODE);
	release_count_before =
		socket_lifecycle_test_counter(RUST_SOCKET_TEST_COUNTER_RELEASE);

	rust_socket_test_fail_next(RUST_SOCKET_TEST_FAIL_SECURITY_POST);
	error = sock_create_kern(&init_net, AF_INET, SOCK_DGRAM, 0, &sock);
	KUNIT_EXPECT_EQ(test, -EPERM, error);
	KUNIT_EXPECT_NULL(test, sock);
	inode_count_after = socket_lifecycle_test_counter(RUST_SOCKET_TEST_COUNTER_INODE);
	release_count_after =
		socket_lifecycle_test_counter(RUST_SOCKET_TEST_COUNTER_RELEASE);
	KUNIT_EXPECT_GT(test, inode_count_after, inode_count_before);
	KUNIT_EXPECT_GT(test, release_count_after, release_count_before);
}
#endif

static int socket_lifecycle_test_worker(void *data)
{
	struct socket_lifecycle_test_worker *worker = data;
	unsigned int index;

	for (index = 0; index < SOCKET_LIFECYCLE_TEST_ITERATIONS; index++) {
		struct socket *sock = NULL;
		int error;

		error = sock_create_kern(&init_net, AF_INET, SOCK_DGRAM, 0,
					 &sock);
		if (error || !sock) {
			atomic_inc(&worker->errors);
			continue;
		}
		sock_release(sock);
	}

	complete(&worker->done);
	while (!kthread_should_stop())
		schedule();
	return 0;
}

static void socket_lifecycle_test_concurrent_create_close(struct kunit *test)
{
	struct socket_lifecycle_test_worker workers[SOCKET_LIFECYCLE_TEST_WORKERS] = {};
	struct task_struct *tasks[SOCKET_LIFECYCLE_TEST_WORKERS] = {};
	unsigned int index;
	bool timed_out = false;

	for (index = 0; index < SOCKET_LIFECYCLE_TEST_WORKERS; index++) {
		init_completion(&workers[index].done);
		tasks[index] = kthread_run(socket_lifecycle_test_worker,
					   &workers[index],
					   "socket-lifecycle-%u", index);
		KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(tasks[index]));
		get_task_struct(tasks[index]);
	}

	for (index = 0; index < SOCKET_LIFECYCLE_TEST_WORKERS; index++) {
		if (!wait_for_completion_timeout(&workers[index].done,
						 SOCKET_LIFECYCLE_TEST_TIMEOUT))
			timed_out = true;
	}
	for (index = 0; index < SOCKET_LIFECYCLE_TEST_WORKERS; index++) {
		KUNIT_EXPECT_EQ(test, 0, kthread_stop_put(tasks[index]));
		KUNIT_EXPECT_EQ(test, 0, atomic_read(&workers[index].errors));
	}

	KUNIT_EXPECT_FALSE(test, timed_out);
}

static int socket_lifecycle_test_init(struct kunit *test)
{
	socket_lifecycle_test_proto.owner = THIS_MODULE;
	socket_lifecycle_test_proto.obj_size = sizeof(struct sock);
	socket_lifecycle_test_proto.name[0] = '\0';
	return 0;
}

static struct kunit_case socket_lifecycle_test_cases[] = {
#ifdef CONFIG_RUST_SOCKET_LIFECYCLE
	KUNIT_CASE(socket_lifecycle_test_failure_injection),
	KUNIT_CASE(socket_lifecycle_test_release_ownership),
	KUNIT_CASE(socket_lifecycle_test_sk_free_deferred),
#endif
	KUNIT_CASE(socket_lifecycle_test_alloc_release),
	KUNIT_CASE(socket_lifecycle_test_create_lite),
	KUNIT_CASE(socket_lifecycle_test_create_kern),
	KUNIT_CASE(socket_lifecycle_test_create_invalid),
	KUNIT_CASE(socket_lifecycle_test_alloc_file),
	KUNIT_CASE(socket_lifecycle_test_sys_socket_file),
	KUNIT_CASE(socket_lifecycle_test_sys_socket),
	KUNIT_CASE(socket_lifecycle_test_sock_put),
	KUNIT_CASE(socket_lifecycle_test_rcu_destruct),
	KUNIT_CASE(socket_lifecycle_test_concurrent_create_close),
	{}
};

static struct kunit_suite socket_lifecycle_test_suite = {
	.name = "socket_lifecycle",
	.init = socket_lifecycle_test_init,
	.test_cases = socket_lifecycle_test_cases,
};

kunit_test_suite(socket_lifecycle_test_suite);

MODULE_DESCRIPTION("KUnit tests for socket and sock lifecycle");
MODULE_LICENSE("GPL");
