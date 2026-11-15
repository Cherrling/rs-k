// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/io.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/personality.h>
#include <sys/prctl.h>
#include <sys/reboot.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <linux/capability.h>
#include <linux/memfd.h>
#include <linux/mman.h>

#define QEMU_EXIT_PORT		0xf4
#define QEMU_EXIT_SUCCESS	0x10
#define QEMU_EXIT_FAILURE	0x11

#define FIXED_BASE	0x10000000000UL

#ifndef MAP_DROPPABLE
#define MAP_DROPPABLE	0x08
#endif

#ifndef MAP_SHARED_VALIDATE
#define MAP_SHARED_VALIDATE	0x03
#endif

#ifndef PR_SET_MDWE
#define PR_SET_MDWE			65
#define PR_MDWE_REFUSE_EXEC_GAIN	1UL
#endif

static size_t page_size;

static void report_result(int passed)
{
	const char *result = passed ?
		"mmap_entry userspace: PASS\n" :
		"mmap_entry userspace: FAIL\n";

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

static int setup_filesystems(void)
{
	if (mkdir("/proc", 0755) && errno != EEXIST)
		return -1;
	if (mount("proc", "/proc", "proc", 0, NULL) && errno != EBUSY)
		return -1;

	if (mkdir("/mnt", 0755) && errno != EEXIST)
		return -1;
	if (mount("tmpfs", "/mnt", "tmpfs", MS_NOEXEC | MS_NOSUID,
		  "size=8m") && errno != EBUSY)
		return -1;

	return 0;
}

static void *raw_mmap(void *addr, size_t length, int prot, int flags,
		      int fd, unsigned long offset)
{
	return (void *)syscall(SYS_mmap, addr, length, prot, flags, fd, offset);
}

static int expect_mmap_error(void *addr, size_t length, int prot, int flags,
			     int fd, unsigned long offset, int expected,
			     const char *name)
{
	void *mapping;
	int saved_errno;

	mapping = raw_mmap(addr, length, prot, flags, fd, offset);
	saved_errno = errno;
	if (mapping != MAP_FAILED) {
		fprintf(stderr, "MMAP_ENTRY %s unexpectedly mapped at %p\n",
			name, mapping);
		munmap(mapping, page_size);
		return 1;
	}
	if (saved_errno != expected) {
		fprintf(stderr, "MMAP_ENTRY %s failed with %s, expected %s\n",
			name, strerror(saved_errno), strerror(expected));
		return 1;
	}
	return 0;
}

static int read_ulong_file(const char *path, unsigned long *value)
{
	FILE *file;
	int result;

	file = fopen(path, "r");
	if (!file)
		return -1;
	result = fscanf(file, "%lu", value) == 1 ? 0 : -1;
	fclose(file);
	return result;
}

static int write_string_file(const char *path, const char *value)
{
	size_t length = strlen(value);
	int fd;
	ssize_t written;

	fd = open(path, O_WRONLY);
	if (fd < 0)
		return -1;
	written = write(fd, value, length);
	close(fd);
	return written == (ssize_t)length ? 0 : -1;
}

static int maps_permissions(unsigned long address, char *permissions,
			    size_t size)
{
	char line[512];
	unsigned long start;
	unsigned long end;
	char perms[16];
	FILE *file;

	if (size < 5)
		return -1;
	file = fopen("/proc/self/maps", "r");
	if (!file)
		return -1;
	while (fgets(line, sizeof(line), file)) {
		if (sscanf(line, "%lx-%lx %15s", &start, &end, perms) != 3)
			continue;
		if (address < start || address >= end)
			continue;
		strncpy(permissions, perms, size - 1);
		permissions[size - 1] = '\0';
		fclose(file);
		return 0;
	}
	fclose(file);
	return -1;
}

static int maps_bounds(unsigned long address, unsigned long *start,
		       unsigned long *end)
{
	char line[512];
	FILE *file;

	file = fopen("/proc/self/maps", "r");
	if (!file)
		return -1;
	while (fgets(line, sizeof(line), file)) {
		if (sscanf(line, "%lx-%lx", start, end) != 2)
			continue;
		if (address < *start || address >= *end)
			continue;
		fclose(file);
		return 0;
	}
	fclose(file);
	return -1;
}

static int committed_as_kb(unsigned long *value)
{
	char line[256];
	FILE *file;

	file = fopen("/proc/meminfo", "r");
	if (!file)
		return -1;
	while (fgets(line, sizeof(line), file)) {
		if (sscanf(line, "Committed_AS: %lu kB", value) == 1) {
			fclose(file);
			return 0;
		}
	}
	fclose(file);
	return -1;
}

static int status_kb(const char *field, unsigned long *value)
{
	size_t field_length = strlen(field);
	char line[256];
	FILE *file;

	file = fopen("/proc/self/status", "r");
	if (!file)
		return -1;
	while (fgets(line, sizeof(line), file)) {
		if (strncmp(line, field, field_length) ||
		    line[field_length] != ':')
			continue;
		if (sscanf(line + field_length + 1, " %lu kB", value) == 1) {
			fclose(file);
			return 0;
		}
		break;
	}
	fclose(file);
	return -1;
}

static int smaps_has_flag(unsigned long address, const char *wanted)
{
	char line[512];
	unsigned long start;
	unsigned long end;
	int in_vma = 0;
	FILE *file;

	file = fopen("/proc/self/smaps", "r");
	if (!file)
		return -1;
	while (fgets(line, sizeof(line), file)) {
		char *token;
		char *save;

		if (sscanf(line, "%lx-%lx", &start, &end) == 2) {
			in_vma = address >= start && address < end;
			continue;
		}
		if (!in_vma)
			continue;
		token = strstr(line, "VmFlags:");
		if (!token)
			continue;
		token = strtok_r(token + strlen("VmFlags:"), " \t\n", &save);
		while (token) {
			if (!strcmp(token, wanted)) {
				fclose(file);
				return 1;
			}
			token = strtok_r(NULL, " \t\n", &save);
		}
		break;
	}
	fclose(file);
	return 0;
}

static void dump_smaps_flags(unsigned long address)
{
	char line[512];
	unsigned long start;
	unsigned long end;
	int in_vma = 0;
	FILE *file;

	file = fopen("/proc/self/smaps", "r");
	if (!file)
		return;
	while (fgets(line, sizeof(line), file)) {
		if (sscanf(line, "%lx-%lx", &start, &end) == 2) {
			in_vma = address >= start && address < end;
			continue;
		}
		if (in_vma && strstr(line, "VmFlags:")) {
			fprintf(stderr, "MMAP_ENTRY %s", line);
			break;
		}
	}
	fclose(file);
}

static int mapping_is_resident(void *address)
{
	unsigned char resident = 0;

	if (mincore(address, page_size, &resident))
		return -1;
	return resident & 1;
}

static int drop_ipc_lock_capability(void)
{
	struct __user_cap_header_struct header = {
		.version = _LINUX_CAPABILITY_VERSION_3,
		.pid = 0,
	};
	struct __user_cap_data_struct data[2];

	if (syscall(SYS_capget, &header, data))
		return -1;
	data[0].effective &= ~(1U << CAP_IPC_LOCK);
	data[0].permitted &= ~(1U << CAP_IPC_LOCK);
	return syscall(SYS_capset, &header, data);
}

static int test_basic_anonymous(void)
{
	unsigned long mmap_min = 0;
	unsigned char *mapping;
	void *hinted;
	pid_t child;
	int status;

	if (expect_mmap_error(NULL, 0, PROT_READ,
			      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0, EINVAL,
			      "zero length"))
		return 1;
	if (expect_mmap_error(NULL, (size_t)-1, PROT_READ,
			      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0, ENOMEM,
			      "length overflow"))
		return 1;
	if (expect_mmap_error(NULL, page_size, PROT_READ,
			      0x0e | MAP_ANONYMOUS, -1, 0, EINVAL,
			      "invalid anonymous type"))
		return 1;
	if (expect_mmap_error(NULL, page_size, PROT_READ,
			      MAP_SHARED | MAP_GROWSDOWN | MAP_ANONYMOUS,
			      -1, 0, EINVAL, "anonymous shared grow"))
		return 1;

	mapping = raw_mmap(NULL, page_size, PROT_READ | PROT_WRITE,
			   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mapping == MAP_FAILED || mapping[0] != 0)
		return 1;
	mapping[0] = 0x5a;
	if (mapping[0] != 0x5a || munmap(mapping, page_size))
		return 1;

	mapping = raw_mmap((void *)FIXED_BASE, page_size,
			   PROT_READ | PROT_WRITE,
			   MAP_PRIVATE | MAP_ANONYMOUS |
			   MAP_FIXED_NOREPLACE, -1, 0);
	if (mapping != (void *)FIXED_BASE)
		return 1;
	if (expect_mmap_error((void *)FIXED_BASE, page_size,
			      PROT_READ | PROT_WRITE,
			      MAP_PRIVATE | MAP_ANONYMOUS |
			      MAP_FIXED_NOREPLACE, -1, 0, EEXIST,
			      "fixed collision"))
		return 1;

	mapping = raw_mmap((void *)FIXED_BASE, page_size,
			   PROT_READ | PROT_WRITE,
			   MAP_SHARED | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	if (mapping != (void *)FIXED_BASE || mapping[0] != 0)
		return 1;

	child = fork();
	if (child < 0)
		return 1;
	if (!child) {
		mapping[0] = 0xa5;
		_exit(0);
	}
	if (waitpid(child, &status, 0) != child ||
	    !WIFEXITED(status) || WEXITSTATUS(status) ||
	    mapping[0] != 0xa5)
		return 1;
	if (munmap(mapping, page_size))
		return 1;

	if (read_ulong_file("/proc/sys/vm/mmap_min_addr", &mmap_min))
		return 1;
	hinted = raw_mmap((void *)0x10, page_size, PROT_READ,
			  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (hinted == MAP_FAILED || (unsigned long)hinted < mmap_min)
		return 1;

	return munmap(hinted, page_size);
}

static int test_new_vma_install(void)
{
	const unsigned long base = FIXED_BASE + (80UL << 20);
	void *mappings[3] = { MAP_FAILED, MAP_FAILED, MAP_FAILED };
	unsigned long start;
	unsigned long end;
	int i;
	int result = 1;

	for (i = 0; i < 3; i++) {
		const unsigned long address = base + i * 2 * page_size;
		unsigned char *mapping;

		mapping = raw_mmap((void *)address, page_size,
				   PROT_READ | PROT_WRITE,
				   MAP_PRIVATE | MAP_ANONYMOUS |
				   MAP_FIXED_NOREPLACE, -1, 0);
		if (mapping != (void *)address)
			goto out;
		mappings[i] = mapping;
		mapping[0] = 0x30 + i;
		if (maps_bounds(address, &start, &end) ||
		    start != address || end != address + page_size)
			goto out;
	}

	for (i = 0; i < 3; i++) {
		const unsigned char *mapping = mappings[i];

		if (mapping[0] != 0x30 + i)
			goto out;
	}
	result = 0;
out:
	for (i = 0; i < 3; i++) {
		if (mappings[i] != MAP_FAILED)
			munmap(mappings[i], page_size);
	}
	return result;
}

static int test_adjacent_merge(void)
{
	const unsigned long base = FIXED_BASE + (16UL << 20);
	const int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE;
	unsigned long start;
	unsigned long end;
	void *first;
	void *second;

	first = raw_mmap((void *)base, page_size, PROT_READ | PROT_WRITE,
			 flags, -1, 0);
	if (first != (void *)base)
		return 1;
	second = raw_mmap((void *)(base + page_size), page_size,
			  PROT_READ | PROT_WRITE, flags, -1, 0);
	if (second != (void *)(base + page_size))
		return 1;
	if (maps_bounds(base, &start, &end) || start != base ||
	    end != base + 2 * page_size) {
		fprintf(stderr, "MMAP_ENTRY adjacent anonymous merge failed\n");
		return 1;
	}
	return munmap((void *)base, 2 * page_size);
}

static int test_setup_accounting(void)
{
	const unsigned long base = FIXED_BASE + (32UL << 20);
	const unsigned long length = 16UL << 20;
	const int flags = MAP_PRIVATE | MAP_ANONYMOUS;
	unsigned long before;
	unsigned long after_map;
	unsigned long after_replace;
	unsigned long after_unmap;
	void *mapping;

	if (committed_as_kb(&before)) {
		fprintf(stderr, "MMAP_ENTRY cannot read initial Committed_AS\n");
		return 1;
	}
	mapping = raw_mmap((void *)base, length, PROT_READ | PROT_WRITE,
			   flags | MAP_FIXED_NOREPLACE, -1, 0);
	if (mapping != (void *)base) {
		fprintf(stderr, "MMAP_ENTRY setup accounting map failed\n");
		return 1;
	}
	if (committed_as_kb(&after_map)) {
		fprintf(stderr, "MMAP_ENTRY cannot read mapped Committed_AS\n");
		return 1;
	}
	if (after_map < before + (8UL << 10)) {
		fprintf(stderr,
			"MMAP_ENTRY setup did not charge committed memory\n");
		return 1;
	}

	mapping = raw_mmap((void *)base, length, PROT_READ | PROT_WRITE,
			   flags | MAP_FIXED, -1, 0);
	if (mapping != (void *)base) {
		fprintf(stderr, "MMAP_ENTRY setup replacement failed\n");
		return 1;
	}
	if (committed_as_kb(&after_replace)) {
		fprintf(stderr,
			"MMAP_ENTRY cannot read replacement Committed_AS\n");
		return 1;
	}
	if (after_replace > after_map + (1UL << 10)) {
		fprintf(stderr,
			"MMAP_ENTRY replacement double charged memory\n");
		return 1;
	}

	if (munmap((void *)base, length))
		return 1;
	if (committed_as_kb(&after_unmap)) {
		fprintf(stderr, "MMAP_ENTRY cannot read unmapped Committed_AS\n");
		return 1;
	}
	if (after_unmap + (8UL << 10) > after_replace) {
		fprintf(stderr, "MMAP_ENTRY unmap did not drop accounting\n");
		return 1;
	}
	return 0;
}

static int test_setup_rollback(void)
{
	const unsigned long base = FIXED_BASE + (48UL << 20);
	pid_t child;
	int status;

	child = fork();
	if (child < 0)
		return 1;
	if (!child) {
		struct rlimit limit = {
			.rlim_cur = 1,
			.rlim_max = 1,
		};
		unsigned char *mapping;

		mapping = raw_mmap((void *)base, page_size,
				   PROT_READ | PROT_WRITE,
				   MAP_PRIVATE | MAP_ANONYMOUS |
				   MAP_FIXED_NOREPLACE, -1, 0);
		if (mapping != (void *)base)
			_exit(1);
		mapping[0] = 0x6d;

		if (setrlimit(RLIMIT_AS, &limit))
			_exit(1);
		if (expect_mmap_error((void *)base, 2 * page_size,
				      PROT_READ | PROT_WRITE,
				      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
				      -1, 0, ENOMEM,
				      "address space limit")) {
			_exit(1);
		}
		if (mapping[0] != 0x6d)
			_exit(1);
		_exit(0);
	}

	if (waitpid(child, &status, 0) != child ||
	    !WIFEXITED(status) || WEXITSTATUS(status))
		return 1;
	return 0;
}

static int test_exec_policy(void)
{
	const char *path = "/mnt/noexec-file";
	static const char content[] = "mmap entry";
	unsigned long old_personality;
	char permissions[8];
	void *mapping;
	int fd;

	fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0600);
	if (fd < 0)
		return 1;
	if (write(fd, content, sizeof(content)) != sizeof(content)) {
		close(fd);
		return 1;
	}
	close(fd);

	old_personality = personality(0xffffffffUL);
	if (old_personality == (unsigned long)-1)
		return 1;
	if ((unsigned long)personality(old_personality | READ_IMPLIES_EXEC) ==
	    (unsigned long)-1)
		return 1;

	mapping = raw_mmap(NULL, page_size, PROT_READ,
			   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mapping == MAP_FAILED ||
	    maps_permissions((unsigned long)mapping, permissions,
			     sizeof(permissions)) ||
	    permissions[2] != 'x')
		return 1;
	munmap(mapping, page_size);

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return 1;
	mapping = raw_mmap(NULL, page_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (mapping == MAP_FAILED ||
	    maps_permissions((unsigned long)mapping, permissions,
			     sizeof(permissions)) ||
	    permissions[2] == 'x') {
		close(fd);
		return 1;
	}
	munmap(mapping, page_size);
	if (expect_mmap_error(NULL, page_size, PROT_READ | PROT_EXEC,
			      MAP_PRIVATE, fd, 0, EPERM, "noexec file")) {
		close(fd);
		return 1;
	}
	close(fd);

	if ((unsigned long)personality(old_personality) == (unsigned long)-1)
		return 1;

	mapping = raw_mmap(NULL, page_size, PROT_EXEC,
			   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mapping == MAP_FAILED ||
	    maps_permissions((unsigned long)mapping, permissions,
			     sizeof(permissions)) ||
	    permissions[2] != 'x')
		return 1;
	if (munmap(mapping, page_size))
		return 1;

	mapping = raw_mmap(NULL, page_size, PROT_READ | PROT_EXEC,
			   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mapping == MAP_FAILED ||
	    maps_permissions((unsigned long)mapping, permissions,
			     sizeof(permissions)) ||
	    permissions[0] != 'r' || permissions[2] != 'x')
		return 1;
	return munmap(mapping, page_size);
}

static int test_mdwe(void)
{
	pid_t child;
	int status;

	child = fork();
	if (child < 0)
		return 1;
	if (!child) {
		void *mapping;

		if (prctl(PR_SET_MDWE, PR_MDWE_REFUSE_EXEC_GAIN, 0L, 0L,
			  0L))
			_exit(1);
		if (expect_mmap_error(NULL, page_size,
				      PROT_READ | PROT_WRITE | PROT_EXEC,
				      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0,
				      EACCES, "MDWE writable execute"))
			_exit(1);

		mapping = raw_mmap(NULL, page_size, PROT_READ | PROT_EXEC,
				   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (mapping == MAP_FAILED || munmap(mapping, page_size))
			_exit(1);
		_exit(0);
	}

	if (waitpid(child, &status, 0) != child ||
	    !WIFEXITED(status) || WEXITSTATUS(status))
		return 1;
	return 0;
}

static int test_file_mapping(void)
{
	const char *path = "/mnt/mapping-file";
	unsigned long overflow_offset = (unsigned long)-page_size;
	char page[4096];
	int mprotect_result;
	int mprotect_errno;
	void *mapping;
	int read_fd;
	int rw_fd;
	int write_fd;
	int memfd;

	memset(page, 0x3c, sizeof(page));
	read_fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0600);
	if (read_fd < 0) {
		fprintf(stderr, "MMAP_ENTRY file setup failed\n");
		return 1;
	}
	if (write(read_fd, page, sizeof(page)) != sizeof(page)) {
		fprintf(stderr, "MMAP_ENTRY file write failed\n");
		close(read_fd);
		return 1;
	}
	close(read_fd);

	read_fd = open(path, O_RDONLY);
	rw_fd = open(path, O_RDWR);
	write_fd = open(path, O_WRONLY);
	if (read_fd < 0 || rw_fd < 0 || write_fd < 0) {
		fprintf(stderr, "MMAP_ENTRY file reopen failed\n");
		return 1;
	}

	if (expect_mmap_error(NULL, page_size, PROT_READ | PROT_WRITE,
			      MAP_SHARED, read_fd, 0, EACCES,
			      "read-only shared write"))
		return 1;
	if (expect_mmap_error(NULL, page_size, PROT_READ, MAP_PRIVATE,
			      write_fd, 0, EACCES,
			      "write-only private read"))
		return 1;
	if (expect_mmap_error(NULL, page_size, PROT_READ,
			      MAP_PRIVATE | MAP_GROWSDOWN, read_fd, 0,
			      EINVAL, "file grow"))
		return 1;
	if (expect_mmap_error(NULL, page_size, PROT_READ,
			      0x0e, read_fd, 0, EINVAL,
			      "invalid file type"))
		return 1;
	if (expect_mmap_error(NULL, page_size, PROT_READ,
			      MAP_SHARED_VALIDATE | MAP_SYNC, read_fd, 0,
			      EOPNOTSUPP, "unsupported shared flag"))
		return 1;
	if (expect_mmap_error(NULL, page_size, PROT_READ, MAP_PRIVATE,
			      read_fd, overflow_offset, EOVERFLOW,
			      "page offset overflow"))
		return 1;

	mapping = raw_mmap(NULL, page_size, PROT_READ | PROT_WRITE,
			   MAP_SHARED, rw_fd, 0);
	if (mapping == MAP_FAILED || ((char *)mapping)[0] != 0x3c) {
		fprintf(stderr, "MMAP_ENTRY writable shared map failed\n");
		return 1;
	}
	munmap(mapping, page_size);

	mapping = raw_mmap(NULL, page_size, PROT_READ | PROT_WRITE,
			   MAP_PRIVATE, read_fd, 0);
	if (mapping == MAP_FAILED || ((char *)mapping)[0] != 0x3c) {
		fprintf(stderr, "MMAP_ENTRY private file map failed\n");
		return 1;
	}
	munmap(mapping, page_size);

	mapping = raw_mmap(NULL, page_size, PROT_READ,
			   MAP_SHARED_VALIDATE, read_fd, 0);
	if (mapping == MAP_FAILED || ((char *)mapping)[0] != 0x3c) {
		fprintf(stderr, "MMAP_ENTRY shared validate map failed\n");
		return 1;
	}
	munmap(mapping, page_size);
	close(read_fd);
	close(rw_fd);
	close(write_fd);

	memfd = syscall(SYS_memfd_create, "sealed", MFD_ALLOW_SEALING);
	if (memfd < 0 || ftruncate(memfd, page_size)) {
		fprintf(stderr, "MMAP_ENTRY memfd setup failed\n");
		return 1;
	}
	if (fcntl(memfd, F_ADD_SEALS, F_SEAL_WRITE)) {
		fprintf(stderr, "MMAP_ENTRY memfd seal failed\n");
		return 1;
	}
	mapping = raw_mmap(NULL, page_size, PROT_READ, MAP_SHARED, memfd, 0);
	if (mapping == MAP_FAILED) {
		fprintf(stderr, "MMAP_ENTRY sealed map failed: %s\n",
			strerror(errno));
		return 1;
	}
	mprotect_result = mprotect(mapping, page_size, PROT_WRITE);
	mprotect_errno = errno;
	if (!mprotect_result || mprotect_errno != EACCES) {
		fprintf(stderr,
			"MMAP_ENTRY sealed mprotect: result=%d errno=%s\n",
			mprotect_result, strerror(mprotect_errno));
		return 1;
	}
	munmap(mapping, page_size);
	if (expect_mmap_error(NULL, page_size, PROT_READ | PROT_WRITE,
			      MAP_SHARED, memfd, 0, EPERM,
			      "write-sealed shared mapping")) {
		close(memfd);
		return 1;
	}
	close(memfd);
	return 0;
}

static int test_zero_file_prepare(void)
{
	unsigned char *private_mapping;
	unsigned char *mapping;
	pid_t child;
	int status;
	int fd;

	fd = open("/dev/zero", O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "MMAP_ENTRY /dev/zero setup failed\n");
		return 1;
	}

	mapping = raw_mmap(NULL, page_size, PROT_READ | PROT_WRITE,
			   MAP_SHARED, fd, 0);
	if (mapping == MAP_FAILED) {
		fprintf(stderr, "MMAP_ENTRY /dev/zero map failed: %s\n",
			strerror(errno));
		return 1;
	}
	mapping[0] = 0x5a;

	private_mapping = raw_mmap(NULL, page_size, PROT_READ | PROT_WRITE,
				   MAP_PRIVATE, fd, 0);
	close(fd);
	if (private_mapping == MAP_FAILED) {
		fprintf(stderr, "MMAP_ENTRY /dev/zero private map failed: %s\n",
			strerror(errno));
		return 1;
	}
	private_mapping[0] = 0x3c;

	child = fork();
	if (child < 0)
		return 1;
	if (!child) {
		mapping[0] = 0xa5;
		_exit(0);
	}
	if (waitpid(child, &status, 0) != child ||
	    !WIFEXITED(status) || WEXITSTATUS(status) ||
	    mapping[0] != 0xa5) {
		fprintf(stderr, "MMAP_ENTRY /dev/zero shared write failed\n");
		return 1;
	}
	if (private_mapping[0] != 0x3c) {
		fprintf(stderr, "MMAP_ENTRY /dev/zero private write failed\n");
		return 1;
	}
	if (munmap(mapping, page_size))
		return 1;
	return munmap(private_mapping, page_size);
}

static int test_secretmem_prepare(void)
{
#ifdef SYS_memfd_secret
	const unsigned long charge_len = 32UL << 20;
	unsigned long before;
	unsigned long after;
	void *mapping;
	int fd;

	fd = syscall(SYS_memfd_secret, 0);
	if (fd < 0) {
		fprintf(stderr, "MMAP_ENTRY secretmem setup failed: %s\n",
			strerror(errno));
		return 1;
	}
	if (committed_as_kb(&before)) {
		fprintf(stderr, "MMAP_ENTRY cannot read Committed_AS\n");
		close(fd);
		return 1;
	}
	if (expect_mmap_error(NULL, charge_len, PROT_READ | PROT_WRITE,
			      MAP_PRIVATE, fd, 0, EINVAL, "secretmem private")) {
		close(fd);
		return 1;
	}
	if (committed_as_kb(&after)) {
		fprintf(stderr, "MMAP_ENTRY cannot reread Committed_AS\n");
		close(fd);
		return 1;
	}
	if (after > before + (1UL << 10)) {
		fprintf(stderr,
			"MMAP_ENTRY committed accounting leaked %lu kB\n",
			after - before);
		close(fd);
		return 1;
	}
	mapping = raw_mmap(NULL, page_size, PROT_READ | PROT_WRITE,
			   MAP_SHARED, fd, 0);
	if (mapping == MAP_FAILED) {
		fprintf(stderr, "MMAP_ENTRY secretmem shared failed: %s\n",
			strerror(errno));
		close(fd);
		return 1;
	}
	if (munmap(mapping, page_size))
		return 1;
	close(fd);
#endif
	return 0;
}

static int test_mapping_policy(void)
{
	unsigned long old_overcommit;
	unsigned long locked_before;
	unsigned long locked_after;
	unsigned long locked_delta;
	unsigned long min_addr;
	void *mapping;

	if (read_ulong_file("/proc/sys/vm/overcommit_memory", &old_overcommit)) {
		fprintf(stderr, "MMAP_ENTRY read overcommit failed\n");
		return 1;
	}
	if (write_string_file("/proc/sys/vm/overcommit_memory", "0\n")) {
		fprintf(stderr, "MMAP_ENTRY set overcommit failed\n");
		return 1;
	}

	mapping = raw_mmap(NULL, 64UL << 20, PROT_NONE,
			   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE,
			   -1, 0);
	if (mapping == MAP_FAILED || smaps_has_flag((unsigned long)mapping,
						    "nr") != 1) {
		fprintf(stderr, "MMAP_ENTRY noreserve flag failed\n");
		return 1;
	}
	munmap(mapping, 64UL << 20);

	mapping = raw_mmap(NULL, page_size, PROT_READ,
			   MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE,
			   -1, 0);
	if (mapping == MAP_FAILED ||
	    mapping_is_resident(mapping) != 1) {
		fprintf(stderr, "MMAP_ENTRY populate failed\n");
		return 1;
	}
	munmap(mapping, page_size);

	mapping = raw_mmap(NULL, page_size, PROT_READ,
			   MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE |
			   MAP_NONBLOCK, -1, 0);
	if (mapping == MAP_FAILED ||
	    mapping_is_resident(mapping) != 0) {
		fprintf(stderr, "MMAP_ENTRY nonblock populate failed\n");
		return 1;
	}
	munmap(mapping, page_size);

	locked_delta = (page_size + 1023) / 1024;
	if (status_kb("VmLck", &locked_before)) {
		fprintf(stderr, "MMAP_ENTRY read initial VmLck failed\n");
		return 1;
	}
	mapping = raw_mmap(NULL, page_size, PROT_READ | PROT_WRITE,
			   MAP_PRIVATE | MAP_ANONYMOUS | MAP_LOCKED,
			   -1, 0);
	if (mapping == MAP_FAILED ||
	    smaps_has_flag((unsigned long)mapping, "lo") != 1) {
		fprintf(stderr, "MMAP_ENTRY locked flag failed\n");
		return 1;
	}
	if (status_kb("VmLck", &locked_after) ||
	    locked_after < locked_before + locked_delta) {
		fprintf(stderr, "MMAP_ENTRY locked accounting failed\n");
		return 1;
	}
	if (munmap(mapping, page_size))
		return 1;
	if (status_kb("VmLck", &locked_after) ||
	    locked_after > locked_before + 1) {
		fprintf(stderr, "MMAP_ENTRY locked rollback failed\n");
		return 1;
	}

	mapping = raw_mmap(NULL, page_size, PROT_READ | PROT_WRITE,
			   MAP_DROPPABLE | MAP_ANONYMOUS,
			   -1, 0);
	if (mapping == MAP_FAILED) {
		fprintf(stderr, "MMAP_ENTRY droppable map failed: %s\n",
			strerror(errno));
		return 1;
	}
	if (smaps_has_flag((unsigned long)mapping, "dp") != 1) {
		dump_smaps_flags((unsigned long)mapping);
		fprintf(stderr, "MMAP_ENTRY droppable flag failed\n");
		return 1;
	}
	munmap(mapping, page_size);
	if (expect_mmap_error(NULL, page_size, PROT_READ,
			      MAP_DROPPABLE | MAP_ANONYMOUS | MAP_LOCKED,
			      -1, 0, EINVAL,
			      "locked droppable"))
		return 1;

	if (write_string_file("/proc/sys/vm/overcommit_memory", "2\n")) {
		fprintf(stderr, "MMAP_ENTRY set overcommit never failed\n");
		return 1;
	}
	mapping = raw_mmap(NULL, page_size, PROT_NONE,
			   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE,
			   -1, 0);
	if (mapping == MAP_FAILED ||
	    smaps_has_flag((unsigned long)mapping, "nr") != 0) {
		fprintf(stderr, "MMAP_ENTRY overcommit never flag failed\n");
		return 1;
	}
	munmap(mapping, page_size);

	if (read_ulong_file("/proc/sys/vm/mmap_min_addr", &min_addr)) {
		fprintf(stderr, "MMAP_ENTRY read mmap min failed\n");
		return 1;
	}
	mapping = raw_mmap((void *)1, page_size, PROT_READ,
			   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mapping == MAP_FAILED || (unsigned long)mapping < min_addr) {
		fprintf(stderr, "MMAP_ENTRY mmap min hint failed\n");
		return 1;
	}
	munmap(mapping, page_size);

	{
		char value[32];

		snprintf(value, sizeof(value), "%lu\n", old_overcommit);
		if (write_string_file("/proc/sys/vm/overcommit_memory", value))
			return 1;
	}
	return 0;
}

static int test_locked_memory_errors(void)
{
	struct rlimit limit;
	pid_t child;
	int status;

	child = fork();
	if (child < 0)
		return 1;
	if (!child) {
		limit.rlim_cur = 0;
		limit.rlim_max = 0;
		if (setrlimit(RLIMIT_MEMLOCK, &limit) ||
		    drop_ipc_lock_capability())
			_exit(1);
		if (expect_mmap_error(NULL, page_size,
				      PROT_READ | PROT_WRITE,
				      MAP_PRIVATE | MAP_ANONYMOUS |
				      MAP_LOCKED, -1, 0, EPERM,
				      "locked without capability"))
			_exit(1);

		limit.rlim_cur = page_size;
		limit.rlim_max = page_size;
		if (setrlimit(RLIMIT_MEMLOCK, &limit))
			_exit(1);
		if (expect_mmap_error(NULL, 2 * page_size,
				      PROT_READ | PROT_WRITE,
				      MAP_PRIVATE | MAP_ANONYMOUS |
				      MAP_LOCKED, -1, 0, EAGAIN,
				      "locked over limit"))
			_exit(1);
		_exit(0);
	}

	if (waitpid(child, &status, 0) != child ||
	    !WIFEXITED(status) || WEXITSTATUS(status))
		return 1;
	return 0;
}

static int test_map_count_limit(void)
{
	unsigned long old_limit;
	char value[32];
	int result = 1;

	if (read_ulong_file("/proc/sys/vm/max_map_count", &old_limit))
		return 1;
	if (write_string_file("/proc/sys/vm/max_map_count", "1\n"))
		return 1;
	if (expect_mmap_error(NULL, page_size, PROT_READ,
			      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0, ENOMEM,
			      "map count limit"))
		goto out;
	result = 0;
out:
	snprintf(value, sizeof(value), "%lu\n", old_limit);
	if (write_string_file("/proc/sys/vm/max_map_count", value))
		result = 1;
	return result;
}

int main(void)
{
	int result = 1;

	setup_console();
	page_size = sysconf(_SC_PAGESIZE);
	if ((long)page_size <= 0 || setup_filesystems()) {
		fprintf(stderr, "MMAP_ENTRY setup failed: %s\n", strerror(errno));
		goto out;
	}

	if (test_basic_anonymous()) {
		fprintf(stderr, "MMAP_ENTRY basic anonymous test failed\n");
		goto out;
	}
	if (test_new_vma_install()) {
		fprintf(stderr, "MMAP_ENTRY new VMA install test failed\n");
		goto out;
	}
	if (test_adjacent_merge()) {
		fprintf(stderr, "MMAP_ENTRY adjacent merge test failed\n");
		goto out;
	}
	if (test_setup_accounting()) {
		fprintf(stderr, "MMAP_ENTRY setup accounting test failed\n");
		goto out;
	}
	if (test_setup_rollback()) {
		fprintf(stderr, "MMAP_ENTRY setup rollback test failed\n");
		goto out;
	}
	if (test_exec_policy()) {
		fprintf(stderr, "MMAP_ENTRY exec policy test failed\n");
		goto out;
	}
	if (test_mdwe()) {
		fprintf(stderr, "MMAP_ENTRY MDWE test failed\n");
		goto out;
	}
	if (test_file_mapping()) {
		fprintf(stderr, "MMAP_ENTRY file mapping test failed\n");
		goto out;
	}
	if (test_zero_file_prepare()) {
		fprintf(stderr, "MMAP_ENTRY /dev/zero prepare test failed\n");
		goto out;
	}
	if (test_secretmem_prepare()) {
		fprintf(stderr, "MMAP_ENTRY secretmem prepare test failed\n");
		goto out;
	}
	if (test_mapping_policy()) {
		fprintf(stderr, "MMAP_ENTRY mapping policy test failed\n");
		goto out;
	}
	if (test_locked_memory_errors()) {
		fprintf(stderr, "MMAP_ENTRY locked memory test failed\n");
		goto out;
	}
	if (test_map_count_limit()) {
		fprintf(stderr, "MMAP_ENTRY map count test failed\n");
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
