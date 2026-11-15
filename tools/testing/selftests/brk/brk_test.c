// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/io.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <linux/userfaultfd.h>

#define QEMU_EXIT_PORT		0xf4
#define QEMU_EXIT_SUCCESS	0x10
#define QEMU_EXIT_FAILURE	0x11

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE	0x100000
#endif

static size_t page_size;

static void report_result(int passed)
{
	const char *result = passed ?
		"brk userspace: PASS\n" :
		"brk userspace: FAIL\n";

	write(STDOUT_FILENO, result, strlen(result));
	usleep(200000);
}

static void setup_console(void)
{
	int console;

	mkdir("/dev", 0755);
	mount("devtmpfs", "/dev", "devtmpfs", 0, NULL);
	console = open("/dev/console", O_RDWR);
	if (console < 0)
		return;
	dup2(console, STDOUT_FILENO);
	dup2(console, STDERR_FILENO);
	if (console > STDERR_FILENO)
		close(console);
}

static void *raw_brk(void *addr)
{
	return (void *)syscall(SYS_brk, addr);
}

static void begin_test(const char *name)
{
	fprintf(stderr, "brk userspace: running %s\n", name);
	fflush(stderr);
}

static unsigned long align_up(unsigned long value)
{
	return (value + page_size - 1) & ~(page_size - 1);
}

static int run_child(int (*test)(void))
{
	pid_t child;
	int status;

	child = fork();
	if (child < 0)
		return 1;
	if (!child)
		_exit(test());
	if (waitpid(child, &status, 0) != child ||
	    !WIFEXITED(status) || WEXITSTATUS(status))
		return 1;
	return 0;
}

static int test_exact_brk(void)
{
	unsigned long current;
	unsigned long next;

	current = (unsigned long)raw_brk(NULL);
	if (current < page_size)
		return 1;

	next = current + 1;
	if ((unsigned long)raw_brk((void *)next) != next ||
	    (unsigned long)raw_brk(NULL) != next)
		return 1;

	if ((unsigned long)raw_brk((void *)current) != current ||
	    (unsigned long)raw_brk(NULL) != current)
		return 1;
	return 0;
}

static int test_growth_and_shrink(void)
{
	unsigned long current;
	unsigned long target;
	unsigned long shrink;
	unsigned long start;
	unsigned char resident;

	current = (unsigned long)raw_brk(NULL);
	target = align_up(current + 1) + 4 * page_size;
	if ((unsigned long)raw_brk((void *)target) != target)
		return 1;

	start = align_up(current);
	if (madvise((void *)start, target - start, MADV_POPULATE_WRITE)) {
		fprintf(stderr, "BRK populate failed: %s\n", strerror(errno));
		return 1;
	}

	shrink = target - 2 * page_size;
	if ((unsigned long)raw_brk((void *)shrink) != shrink ||
	    (unsigned long)raw_brk(NULL) != shrink)
		return 1;

	resident = 0;
	if (!mincore((void *)shrink, page_size, &resident) || errno != ENOMEM) {
		fprintf(stderr, "BRK shrink left the removed page mapped\n");
		return 1;
	}

	if ((unsigned long)raw_brk((void *)current) != current ||
	    (unsigned long)raw_brk(NULL) != current)
		return 1;
	return 0;
}

static int test_too_low(void)
{
	unsigned long current;

	current = (unsigned long)raw_brk(NULL);
	if ((unsigned long)raw_brk((void *)page_size) != current ||
	    (unsigned long)raw_brk(NULL) != current)
		return 1;
	return 0;
}

static int data_limit_child(void)
{
	struct rlimit limit;
	unsigned long current;
	unsigned long target;

	current = (unsigned long)raw_brk(NULL);
	if (getrlimit(RLIMIT_DATA, &limit))
		return 1;
	limit.rlim_cur = 0;
	if (setrlimit(RLIMIT_DATA, &limit))
		return 1;

	target = align_up(current + 1);
	if ((unsigned long)raw_brk((void *)target) != current ||
	    (unsigned long)raw_brk(NULL) != current)
		return 1;
	return 0;
}

static int gap_child(void)
{
	unsigned long current;
	unsigned long floor;
	unsigned long target;
	void *mapping;

	current = (unsigned long)raw_brk(NULL);
	floor = align_up(current);
	target = floor + page_size;
	mapping = mmap((void *)(floor + page_size), page_size, PROT_NONE,
		       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
		       -1, 0);
	if (mapping == MAP_FAILED) {
		fprintf(stderr, "BRK gap setup failed: %s\n", strerror(errno));
		return 1;
	}

	if ((unsigned long)raw_brk((void *)target) != current ||
	    (unsigned long)raw_brk(NULL) != current) {
		fprintf(stderr, "BRK expanded into an adjacent mapping\n");
		return 1;
	}
	munmap(mapping, page_size);
	return 0;
}

static int populated_child(void)
{
	unsigned long current;
	unsigned long start;
	unsigned long target;
	unsigned long addr;
	unsigned char resident;

	if (mlockall(MCL_FUTURE)) {
		fprintf(stderr, "BRK mlockall failed: %s\n", strerror(errno));
		return 1;
	}

	current = (unsigned long)raw_brk(NULL);
	start = align_up(current);
	target = align_up(current + 1) + 2 * page_size;
	if ((unsigned long)raw_brk((void *)target) != target) {
		fprintf(stderr, "BRK populated grow failed\n");
		return 1;
	}

	for (addr = start; addr < target; addr += page_size) {
		resident = 0;
		if (mincore((void *)addr, page_size, &resident)) {
			fprintf(stderr, "BRK mincore failed: %s\n", strerror(errno));
			return 1;
		}
		if (!(resident & 1)) {
			fprintf(stderr, "BRK locked mapping was not populated\n");
			return 1;
		}
	}

	if (munlockall())
		return 1;
	return 0;
}

struct unmap_event_test {
	int uffd;
	unsigned long start;
	unsigned long end;
	int result;
};

static void *read_unmap_event(void *data)
{
	struct unmap_event_test *test = data;
	struct uffd_msg msg;
	struct pollfd pollfd = {
		.fd = test->uffd,
		.events = POLLIN,
	};
	ssize_t bytes;

	test->result = 1;
	if (poll(&pollfd, 1, 5000) != 1) {
		fprintf(stderr, "BRK userfaultfd unmap event timed out\n");
		return NULL;
	}

	bytes = read(test->uffd, &msg, sizeof(msg));
	if (bytes != sizeof(msg) || msg.event != UFFD_EVENT_UNMAP ||
	    msg.arg.remove.start != test->start ||
	    msg.arg.remove.end != test->end) {
		fprintf(stderr, "BRK userfaultfd unmap event mismatch\n");
		return NULL;
	}
	test->result = 0;
	return NULL;
}

static int userfaultfd_child(void)
{
	struct uffdio_register reg = { 0 };
	struct uffdio_api api = {
		.api = UFFD_API,
		.features = UFFD_FEATURE_EVENT_UNMAP,
	};
	struct unmap_event_test test;
	pthread_t reader;
	unsigned long current;
	unsigned long start;
	unsigned long target;
	int uffd;

	begin_test("userfaultfd open");
	uffd = syscall(SYS_userfaultfd, O_CLOEXEC | O_NONBLOCK);
	if (uffd < 0) {
		fprintf(stderr, "BRK userfaultfd setup failed: %s\n",
			strerror(errno));
		return 1;
	}
	begin_test("userfaultfd api");
	if (ioctl(uffd, UFFDIO_API, &api)) {
		fprintf(stderr, "BRK userfaultfd API failed: %s\n", strerror(errno));
		return 1;
	}

	current = (unsigned long)raw_brk(NULL);
	start = align_up(current) + page_size;
	target = align_up(current + 1) + 3 * page_size;
	begin_test("userfaultfd initial growth");
	if ((unsigned long)raw_brk((void *)target) != target)
		return 1;

	begin_test("userfaultfd register");
	reg.range.start = start;
	reg.range.len = target - start;
	reg.mode = UFFDIO_REGISTER_MODE_MISSING;
	if (ioctl(uffd, UFFDIO_REGISTER, &reg)) {
		fprintf(stderr, "BRK userfaultfd register failed: %s\n",
			strerror(errno));
		return 1;
	}

	test.uffd = uffd;
	test.start = start;
	test.end = target;
	if (pthread_create(&reader, NULL, read_unmap_event, &test)) {
		fprintf(stderr, "BRK userfaultfd reader failed\n");
		return 1;
	}

	begin_test("userfaultfd shrink");
	if ((unsigned long)raw_brk((void *)start) != start)
		return 1;

	if (pthread_join(reader, NULL)) {
		fprintf(stderr, "BRK userfaultfd reader join failed\n");
		return 1;
	}
	if (test.result)
		return 1;
	close(uffd);
	return 0;
}

int main(void)
{
	int result = 1;

	setup_console();
	page_size = sysconf(_SC_PAGESIZE);
	if (!page_size) {
		fprintf(stderr, "BRK page size failed\n");
		goto out;
	}

	begin_test("exact update");
	if (test_exact_brk()) {
		fprintf(stderr, "BRK exact update test failed\n");
		goto out;
	}
	begin_test("growth and shrink");
	if (test_growth_and_shrink()) {
		fprintf(stderr, "BRK growth and shrink test failed\n");
		goto out;
	}
	begin_test("minimum");
	if (test_too_low()) {
		fprintf(stderr, "BRK minimum test failed\n");
		goto out;
	}
	begin_test("data limit");
	if (run_child(data_limit_child)) {
		fprintf(stderr, "BRK data limit test failed\n");
		goto out;
	}
	begin_test("stack gap");
	if (run_child(gap_child)) {
		fprintf(stderr, "BRK stack-gap test failed\n");
		goto out;
	}
	begin_test("populated expansion");
	if (run_child(populated_child)) {
		fprintf(stderr, "BRK populated expansion test failed\n");
		goto out;
	}
	begin_test("userfaultfd unmap");
	if (run_child(userfaultfd_child)) {
		fprintf(stderr, "BRK userfaultfd test failed\n");
		goto out;
	}
	result = 0;
out:
	report_result(!result);
	if (!iopl(3))
		outb(result ? QEMU_EXIT_FAILURE : QEMU_EXIT_SUCCESS,
		     QEMU_EXIT_PORT);
	reboot(RB_POWER_OFF);
	return result;
}
