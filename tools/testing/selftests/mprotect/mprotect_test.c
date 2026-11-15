// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/io.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define QEMU_EXIT_PORT		0xf4
#define QEMU_EXIT_SUCCESS	0x10
#define QEMU_EXIT_FAILURE	0x11

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE	0x100000
#endif

static size_t page_size;
static sigjmp_buf fault_jump;
/* Signal handlers require a volatile sig_atomic_t object. */
static volatile sig_atomic_t fault_seen;

static void report_result(int passed)
{
	const char *result = passed ?
		"mprotect userspace: PASS\n" :
		"mprotect userspace: FAIL\n";

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

static void begin_test(const char *name)
{
	fprintf(stderr, "mprotect userspace: running %s\n", name);
	fflush(stderr);
}

static void *map_range(size_t length, int prot, int flags)
{
	void *mapping;

	mapping = mmap(NULL, length, prot, flags, -1, 0);
	if (mapping == MAP_FAILED) {
		fprintf(stderr, "mprotect mmap failed: %s\n", strerror(errno));
		return NULL;
	}
	return mapping;
}

static long raw_mprotect(void *addr, size_t length, unsigned long prot)
{
	return syscall(SYS_mprotect, addr, length, prot);
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

static void fault_handler(int signal)
{
	(void)signal;
	fault_seen = 1;
	siglongjmp(fault_jump, 1);
}

static int setup_fault_handler(void)
{
	struct sigaction action = {
		.sa_handler = fault_handler,
	};

	sigemptyset(&action.sa_mask);
	if (sigaction(SIGSEGV, &action, NULL)) {
		fprintf(stderr, "mprotect SIGSEGV handler setup failed: %s\n",
			strerror(errno));
		return 1;
	}
	return 0;
}

static int expect_fault(volatile unsigned char *addr, int write_fault)
{
	unsigned char value;

	fault_seen = 0;
	if (sigsetjmp(fault_jump, 1))
		return 0;

	if (write_fault)
		*addr = 0xa5;
	else
		value = *addr;
	(void)value;
	return 1;
}

static void fill_pattern(unsigned char *mapping, size_t length)
{
	size_t offset;

	for (offset = 0; offset < length; offset++)
		mapping[offset] = (unsigned char)(offset * 17 + 3);
}

static int verify_pattern(unsigned char *mapping, size_t length)
{
	size_t offset;

	for (offset = 0; offset < length; offset++) {
		if (mapping[offset] != (unsigned char)(offset * 17 + 3)) {
			fprintf(stderr, "mprotect data mismatch at offset %zu\n",
				offset);
			return 1;
		}
	}
	return 0;
}

static int setup_procfs(void)
{
	mkdir("/proc", 0555);
	if (!mount("proc", "/proc", "proc", 0, NULL) || errno == EBUSY)
		return 0;
	fprintf(stderr, "mprotect proc mount failed: %s\n", strerror(errno));
	return 1;
}

static void disable_expected_fault_logging(void)
{
	int fd;

	fd = open("/proc/sys/debug/exception-trace", O_WRONLY);
	if (fd < 0)
		return;
	write(fd, "0\n", 2);
	close(fd);
}

static int maps_permissions(void *addr, char permissions[5])
{
	unsigned long target = (unsigned long)addr;
	unsigned long start;
	unsigned long end;
	char line[512];
	FILE *maps;

	maps = fopen("/proc/self/maps", "r");
	if (!maps)
		return 1;
	while (fgets(line, sizeof(line), maps)) {
		if (sscanf(line, "%lx-%lx %4s", &start, &end, permissions) != 3)
			continue;
		if (target >= start && target < end) {
			fclose(maps);
			return 0;
		}
	}
	fclose(maps);
	return 1;
}

static int maps_count(void *addr, size_t length)
{
	unsigned long range_start = (unsigned long)addr;
	unsigned long range_end = range_start + length;
	unsigned long start;
	unsigned long end;
	char permissions[5];
	char line[512];
	FILE *maps;
	int count = 0;

	maps = fopen("/proc/self/maps", "r");
	if (!maps)
		return -1;
	while (fgets(line, sizeof(line), maps)) {
		if (sscanf(line, "%lx-%lx %4s", &start, &end,
			   permissions) != 3)
			continue;
		if (end <= range_start || start >= range_end)
			continue;
		count++;
	}
	fclose(maps);
	return count;
}

static int test_permissions_and_data(void)
{
	const size_t length = 4 * page_size;
	unsigned char *mapping;

	mapping = map_range(length, PROT_READ | PROT_WRITE,
			    MAP_PRIVATE | MAP_ANONYMOUS);
	if (!mapping)
		return 1;
	fill_pattern(mapping, length);

	if (raw_mprotect(mapping, length, PROT_READ)) {
		fprintf(stderr, "mprotect read-only transition failed: %s\n",
			strerror(errno));
		return 1;
	}
	if (verify_pattern(mapping, length)) {
		fprintf(stderr, "mprotect read-only data changed\n");
		return 1;
	}
	if (expect_fault(mapping + page_size / 2, 1)) {
		fprintf(stderr, "mprotect read-only mapping remained writable\n");
		return 1;
	}

	if (raw_mprotect(mapping, length, PROT_READ | PROT_WRITE) ||
	    raw_mprotect(mapping, length, PROT_READ | PROT_EXEC) ||
	    verify_pattern(mapping, length) ||
	    expect_fault(mapping + page_size / 2, 1)) {
		fprintf(stderr, "mprotect executable transition failed\n");
		return 1;
	}

	if (raw_mprotect(mapping, length, PROT_NONE) ||
	    expect_fault(mapping, 0) ||
	    raw_mprotect(mapping, 4 * page_size, PROT_READ) ||
	    verify_pattern(mapping, length)) {
		fprintf(stderr, "mprotect PROT_NONE recovery failed\n");
		return 1;
	}

	if (raw_mprotect(mapping, length, PROT_READ) ||
	    verify_pattern(mapping, length)) {
		fprintf(stderr, "mprotect same-flags transition failed\n");
		return 1;
	}

	munmap(mapping, length);
	return 0;
}

static int test_validation(void)
{
	const size_t length = 2 * page_size;
	unsigned char *mapping;

	mapping = map_range(length, PROT_READ | PROT_WRITE,
			    MAP_PRIVATE | MAP_ANONYMOUS);
	if (!mapping)
		return 1;

	if (raw_mprotect(mapping + 1, page_size, PROT_READ) != -1 ||
	    errno != EINVAL) {
		fprintf(stderr, "mprotect accepted an unaligned address\n");
		return 1;
	}
	if (raw_mprotect(mapping, 0, PROT_READ)) {
		fprintf(stderr, "mprotect rejected an empty range\n");
		return 1;
	}
	if (raw_mprotect(mapping, SIZE_MAX, PROT_READ) != -1 ||
	    errno != ENOMEM) {
		fprintf(stderr, "mprotect accepted a length overflow\n");
		return 1;
	}
	if (raw_mprotect(mapping, page_size,
			 PROT_READ | PROT_GROWSDOWN | PROT_GROWSUP) != -1 ||
	    errno != EINVAL) {
		fprintf(stderr, "mprotect accepted conflicting growth flags\n");
		return 1;
	}
	if (raw_mprotect(mapping, page_size, PROT_READ | PROT_GROWSDOWN) != -1 ||
	    errno != EINVAL) {
		fprintf(stderr, "mprotect accepted GROWSDOWN on a normal VMA\n");
		return 1;
	}
	if (raw_mprotect(mapping, page_size, PROT_READ | PROT_GROWSUP) != -1 ||
	    errno != EINVAL) {
		fprintf(stderr, "mprotect accepted GROWSUP on a normal VMA\n");
		return 1;
	}

	munmap(mapping, length);
	return 0;
}

static int test_range_hole(void)
{
	const size_t length = 3 * page_size;
	unsigned char *mapping;

	mapping = map_range(length, PROT_READ | PROT_WRITE,
			    MAP_PRIVATE | MAP_ANONYMOUS);
	if (!mapping)
		return 1;
	if (munmap(mapping + page_size, page_size)) {
		fprintf(stderr, "mprotect hole setup failed: %s\n",
			strerror(errno));
		return 1;
	}
	if (raw_mprotect(mapping, length, PROT_READ) != -1 ||
	    errno != ENOMEM) {
		fprintf(stderr, "mprotect accepted a range with a hole\n");
		return 1;
	}
	munmap(mapping, page_size);
	munmap(mapping + 2 * page_size, page_size);
	return 0;
}

static int test_split_and_merge(void)
{
	const size_t length = 3 * page_size;
	unsigned char *mapping;
	char first[5];
	char second[5];
	char third[5];
	int count;

	mapping = map_range(length, PROT_READ | PROT_WRITE,
			    MAP_PRIVATE | MAP_ANONYMOUS);
	if (!mapping)
		return 1;
	fill_pattern(mapping, length);

	if (maps_count(mapping, length) != 1) {
		fprintf(stderr, "mprotect initial VMA count is not one\n");
		return 1;
	}
	if (raw_mprotect(mapping + page_size, page_size, PROT_READ)) {
		fprintf(stderr, "mprotect middle-page split failed: %s\n",
			strerror(errno));
		return 1;
	}
	count = maps_count(mapping, length);
	if (count != 3 ||
	    maps_permissions(mapping, first) ||
	    maps_permissions(mapping + page_size, second) ||
	    maps_permissions(mapping + 2 * page_size, third) ||
	    strncmp(first, "rw-", 3) || strncmp(second, "r--", 3) ||
	    strncmp(third, "rw-", 3)) {
		fprintf(stderr,
			"mprotect split permissions are incorrect: count=%d perms=%s,%s,%s\n",
			count, first, second, third);
		return 1;
	}
	if (expect_fault(mapping + page_size, 1)) {
		fprintf(stderr, "mprotect readonly split page remained writable\n");
		return 1;
	}
	if (verify_pattern(mapping, length)) {
		fprintf(stderr, "mprotect split changed data\n");
		return 1;
	}

	if (raw_mprotect(mapping, length, PROT_READ | PROT_WRITE)) {
		fprintf(stderr, "mprotect merge restore failed: %s\n",
			strerror(errno));
		return 1;
	}
	count = maps_count(mapping, length);
	if (count != 1 ||
	    maps_permissions(mapping + page_size, second) ||
	    strncmp(second, "rw-", 3) ||
	    verify_pattern(mapping, length)) {
		fprintf(stderr,
			"mprotect split did not merge cleanly: count=%d perms=%s\n",
			count, second);
		return 1;
	}

	munmap(mapping, length);
	return 0;
}

static int test_growsdown(void)
{
	const size_t length = 2 * page_size;
	unsigned char *mapping;

	mapping = mmap(NULL, length, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS | MAP_GROWSDOWN, -1, 0);
	if (mapping == MAP_FAILED) {
		fprintf(stderr, "mprotect GROWSDOWN mapping failed: %s\n",
			strerror(errno));
		return 1;
	}
	fill_pattern(mapping, length);

	if (raw_mprotect(mapping + page_size, page_size,
			 PROT_READ | PROT_GROWSDOWN)) {
		fprintf(stderr, "mprotect GROWSDOWN request failed: %s\n",
			strerror(errno));
		return 1;
	}
	if (verify_pattern(mapping, length) ||
	    expect_fault(mapping, 1) ||
	    expect_fault(mapping + page_size, 1)) {
		fprintf(stderr, "mprotect GROWSDOWN range is incorrect\n");
		return 1;
	}

	if (raw_mprotect(mapping, length, PROT_READ | PROT_WRITE)) {
		fprintf(stderr, "mprotect GROWSDOWN restore failed\n");
		return 1;
	}
	mapping[0] = 0x5a;
	munmap(mapping, length);
	return 0;
}

static int test_pkey(void)
{
#ifdef SYS_pkey_mprotect
	const size_t length = page_size;
	unsigned char *mapping;
	int pkey = -1;

	mapping = map_range(length, PROT_READ | PROT_WRITE,
			    MAP_PRIVATE | MAP_ANONYMOUS);
	if (!mapping)
		return 1;

#ifdef SYS_pkey_alloc
	pkey = syscall(SYS_pkey_alloc, 0, 0);
#endif
	if (pkey >= 0) {
		fprintf(stderr, "mprotect pkey: allocated path\n");
		if (syscall(SYS_pkey_mprotect, mapping, length,
			    PROT_READ | PROT_WRITE, pkey)) {
			fprintf(stderr, "pkey_mprotect rejected an allocated pkey\n");
			return 1;
		}
		if (syscall(SYS_pkey_free, pkey)) {
			fprintf(stderr, "pkey_free failed: %s\n", strerror(errno));
			return 1;
		}
		if (syscall(SYS_pkey_mprotect, mapping, length,
			    PROT_READ | PROT_WRITE, pkey) != -1 ||
		    errno != EINVAL) {
			fprintf(stderr, "pkey_mprotect accepted a freed pkey\n");
			return 1;
		}
	} else {
		fprintf(stderr, "mprotect pkey: unavailable, invalid-key path\n");
		if (syscall(SYS_pkey_mprotect, mapping, length,
			    PROT_READ | PROT_WRITE, 15) != -1 ||
		    errno != EINVAL) {
			fprintf(stderr, "pkey_mprotect accepted an invalid pkey\n");
			return 1;
		}
	}

	munmap(mapping, length);
	return 0;
#else
	return 0;
#endif
}

static int sealed_child(void)
{
#ifdef SYS_mseal
	const size_t length = 2 * page_size;
	unsigned char *mapping;

	mapping = map_range(length, PROT_READ | PROT_WRITE,
			    MAP_PRIVATE | MAP_ANONYMOUS);
	if (!mapping)
		return 1;
	if (syscall(SYS_mseal, mapping, length, 0)) {
		fprintf(stderr, "mseal failed: %s\n", strerror(errno));
		return 1;
	}
	if (raw_mprotect(mapping, length, PROT_READ) != -1 ||
	    errno != EPERM) {
		fprintf(stderr, "mprotect accepted a sealed VMA\n");
		return 1;
	}
	return 0;
#else
	return 0;
#endif
}

static int populated_child(void)
{
	const size_t length = 4 * page_size;
	struct rlimit limit = {
		.rlim_cur = RLIM_INFINITY,
		.rlim_max = RLIM_INFINITY,
	};
	unsigned char *mapping;
	unsigned char resident;
	size_t offset;

	setrlimit(RLIMIT_MEMLOCK, &limit);
	if (mlockall(MCL_CURRENT | MCL_FUTURE)) {
		fprintf(stderr, "mprotect mlockall failed: %s\n",
			strerror(errno));
		return 1;
	}

	mapping = mmap(NULL, length, PROT_READ,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mapping == MAP_FAILED) {
		fprintf(stderr, "mprotect locked mmap failed: %s\n",
			strerror(errno));
		return 1;
	}
	if (raw_mprotect(mapping, length, PROT_READ | PROT_WRITE)) {
		fprintf(stderr, "mprotect locked transition failed: %s\n",
			strerror(errno));
		return 1;
	}

	for (offset = 0; offset < length; offset += page_size) {
		resident = 0;
		if (mincore(mapping + offset, page_size, &resident)) {
			fprintf(stderr, "mprotect mincore failed: %s\n",
				strerror(errno));
			return 1;
		}
		if (!(resident & 1)) {
			fprintf(stderr, "mprotect did not populate locked page\n");
			return 1;
		}
	}

	munmap(mapping, length);
	munlockall();
	return 0;
}

static int test_file_mapping(void)
{
	const size_t length = page_size;
	const char *path = "/mprotect-selftest";
	unsigned char *mapping;
	int fd;

	fd = open(path, O_CREAT | O_EXCL | O_RDWR, 0600);
	if (fd < 0) {
		fprintf(stderr, "mprotect file create failed: %s\n",
			strerror(errno));
		return 1;
	}
	if (ftruncate(fd, length)) {
		fprintf(stderr, "mprotect file truncate failed: %s\n",
			strerror(errno));
		return 1;
	}
	close(fd);

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "mprotect read-only file open failed: %s\n",
			strerror(errno));
		return 1;
	}
	mapping = mmap(NULL, length, PROT_READ, MAP_SHARED, fd, 0);
	if (mapping == MAP_FAILED) {
		fprintf(stderr, "mprotect read-only file mmap failed: %s\n",
			strerror(errno));
		return 1;
	}
	if (raw_mprotect(mapping, length, PROT_READ | PROT_WRITE) != -1 ||
	    errno != EACCES) {
		fprintf(stderr, "mprotect added write access to a read-only file VMA\n");
		return 1;
	}
	munmap(mapping, length);
	close(fd);

	fd = open(path, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "mprotect writable file open failed: %s\n",
			strerror(errno));
		return 1;
	}
	mapping = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (mapping == MAP_FAILED) {
		fprintf(stderr, "mprotect writable file mmap failed: %s\n",
			strerror(errno));
		return 1;
	}
	fill_pattern(mapping, length);
	if (raw_mprotect(mapping, length, PROT_READ) ||
	    verify_pattern(mapping, length)) {
		fprintf(stderr, "mprotect file transition failed\n");
		return 1;
	}

	munmap(mapping, length);
	close(fd);
	unlink(path);
	return 0;
}

struct stress_context {
	int id;
	int result;
};

static void *stress_thread(void *data)
{
	const size_t length = 16 * page_size;
	struct stress_context *context = data;
	unsigned char *mapping;
	unsigned char value;
	size_t offset;
	int iteration;

	context->result = 1;
	mapping = mmap(NULL, length, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mapping == MAP_FAILED)
		return NULL;
	fill_pattern(mapping, length);

	for (iteration = 0; iteration < 100; iteration++) {
		if (raw_mprotect(mapping, length, PROT_READ))
			goto out;
		for (offset = 0; offset < length; offset++) {
			value = mapping[offset];
			if (value != (unsigned char)(offset * 17 + 3))
				goto out;
		}

		if (raw_mprotect(mapping, length, PROT_READ | PROT_WRITE))
			goto out;
		offset = (size_t)(context->id * 257 + iteration * 13) % length;
		mapping[offset] = 0xff;
		if (mapping[offset] != 0xff)
			goto out;
		mapping[offset] = (unsigned char)(offset * 17 + 3);

		if (raw_mprotect(mapping + 6 * page_size, 4 * page_size,
				 PROT_READ))
			goto out;
		mapping[0] = 3;
		mapping[length - 1] =
			(unsigned char)((length - 1) * 17 + 3);
		for (offset = 6 * page_size; offset < 10 * page_size; offset++) {
			value = mapping[offset];
			(void)value;
		}
		if (raw_mprotect(mapping, length, PROT_READ | PROT_WRITE))
			goto out;
	}

	context->result = 0;
out:
	munmap(mapping, length);
	return NULL;
}

static int test_concurrent_updates(void)
{
	struct stress_context contexts[4];
	pthread_t threads[4];
	int index;

	for (index = 0; index < 4; index++) {
		contexts[index].id = index;
		contexts[index].result = 1;
		if (pthread_create(&threads[index], NULL, stress_thread,
				   &contexts[index])) {
			fprintf(stderr, "mprotect stress thread creation failed\n");
			return 1;
		}
	}
	for (index = 0; index < 4; index++) {
		if (pthread_join(threads[index], NULL) ||
		    contexts[index].result) {
			fprintf(stderr, "mprotect stress thread %d failed\n",
				index);
			return 1;
		}
	}
	return 0;
}

int main(void)
{
	int result = 1;

	setup_console();
	page_size = sysconf(_SC_PAGESIZE);
	if (!page_size) {
		fprintf(stderr, "mprotect page size failed\n");
		goto out;
	}
	if (setup_fault_handler())
		goto out;
	if (setup_procfs())
		goto out;
	disable_expected_fault_logging();

	begin_test("permission transitions");
	if (test_permissions_and_data())
		goto out;
	begin_test("request validation");
	if (test_validation())
		goto out;
	begin_test("range hole");
	if (test_range_hole())
		goto out;
	begin_test("split and merge");
	if (test_split_and_merge())
		goto out;
	begin_test("GROWSDOWN");
	if (test_growsdown())
		goto out;
	begin_test("pkey");
	if (test_pkey())
		goto out;
	begin_test("sealed VMA");
	if (run_child(sealed_child))
		goto out;
	begin_test("locked population");
	if (run_child(populated_child))
		goto out;
	begin_test("file mapping");
	if (test_file_mapping())
		goto out;
	begin_test("concurrent updates");
	if (test_concurrent_updates())
		goto out;
	result = 0;
out:
	report_result(!result);
	if (!iopl(3))
		outb(result ? QEMU_EXIT_FAILURE : QEMU_EXIT_SUCCESS,
		     QEMU_EXIT_PORT);
	reboot(RB_POWER_OFF);
	return result;
}
