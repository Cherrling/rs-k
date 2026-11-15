// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <linux/aio_abi.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/io.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#define CARVEOUT_PAGES	512
#define QEMU_EXIT_PORT	0xf4
#define QEMU_EXIT_SUCCESS	0x10
#define QEMU_EXIT_FAILURE	0x11

static size_t page_size;

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

static int setup_proc(void)
{
	if (mkdir("/proc", 0755) && errno != EEXIST)
		return -1;
	return mount("proc", "/proc", "proc", 0, NULL);
}

static void report_result(int passed)
{
	const char *result = passed ?
		"copy_vma userspace: PASS\n" :
		"copy_vma userspace: FAIL\n";

	usleep(200000);
	write(STDOUT_FILENO, result, strlen(result));
	usleep(200000);
}

static int find_vma_bounds(unsigned long address, unsigned long *start,
			   unsigned long *end)
{
	char line[512];
	FILE *maps;
	int found = 0;

	maps = fopen("/proc/self/maps", "r");
	if (!maps)
		return -1;

	while (fgets(line, sizeof(line), maps)) {
		unsigned long vma_start;
		unsigned long vma_end;

		if (sscanf(line, "%lx-%lx", &vma_start, &vma_end) != 2)
			continue;
		if (address < vma_start || address >= vma_end)
			continue;
		*start = vma_start;
		*end = vma_end;
		found = 1;
		break;
	}

	fclose(maps);
	return found ? 0 : -1;
}

static int read_pagemap_entry(unsigned long address, uint64_t *entry)
{
	off_t offset;
	ssize_t size;
	int fd;

	fd = open("/proc/self/pagemap", O_RDONLY);
	if (fd < 0)
		return -1;

	offset = (address / page_size) * sizeof(*entry);
	size = pread(fd, entry, sizeof(*entry), offset);
	close(fd);
	return size == sizeof(*entry) ? 0 : -1;
}

static int read_pagemap(unsigned long address, int *present,
			unsigned long *pfn)
{
	uint64_t entry;

	if (read_pagemap_entry(address, &entry))
		return -1;

	*present = !!(entry & (1ULL << 63));
	*pfn = entry & ((1ULL << 55) - 1);
	return 0;
}

static int find_named_vma(const char *name, unsigned long *start,
			  unsigned long *end)
{
	char line[512];
	FILE *maps;
	int found = 0;

	maps = fopen("/proc/self/maps", "r");
	if (!maps)
		return -1;

	while (fgets(line, sizeof(line), maps)) {
		unsigned long vma_start;
		unsigned long vma_end;

		if (!strstr(line, name))
			continue;
		if (sscanf(line, "%lx-%lx", &vma_start, &vma_end) != 2)
			continue;
		*start = vma_start;
		*end = vma_end;
		found = 1;
		break;
	}

	fclose(maps);
	return found ? 0 : -1;
}

static int write_string_file(const char *path, const char *value)
{
	size_t length = strlen(value);
	ssize_t written;
	int fd;

	fd = open(path, O_WRONLY);
	if (fd < 0)
		return -1;
	written = write(fd, value, length);
	close(fd);
	return written == (ssize_t)length ? 0 : -1;
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

static int read_total_vm_pages(unsigned long *pages)
{
	return read_ulong_file("/proc/self/statm", pages);
}

static int read_locked_bytes(unsigned long address, unsigned long *bytes)
{
	char line[512];
	FILE *file;
	int in_vma = 0;
	int result = -1;

	file = fopen("/proc/self/smaps", "r");
	if (!file)
		return -1;

	while (fgets(line, sizeof(line), file)) {
		unsigned long start;
		unsigned long end;

		if (sscanf(line, "%lx-%lx", &start, &end) == 2) {
			in_vma = address >= start && address < end;
			continue;
		}
		if (in_vma &&
		    sscanf(line, "Locked: %lu kB", bytes) == 1) {
			result = 0;
			break;
		}
	}

	fclose(file);
	return result;
}

static int setup_debugfs(void)
{
	if (mkdir("/sys", 0755) && errno != EEXIST)
		return -1;
	if (mkdir("/sys/kernel", 0755) && errno != EEXIST)
		return -1;
	if (mkdir("/sys/kernel/debug", 0755) && errno != EEXIST)
		return -1;
	if (mount("debugfs", "/sys/kernel/debug", "debugfs", 0, NULL) &&
	    errno != EBUSY)
		return -1;
	return 0;
}

static int write_fail_page_alloc(const char *name, const char *value)
{
	char path[256];

	snprintf(path, sizeof(path),
		 "/sys/kernel/debug/fail_page_alloc/%s", name);
	return write_string_file(path, value);
}

static int read_fail_page_alloc(const char *name, unsigned long *value)
{
	char path[256];

	snprintf(path, sizeof(path),
		 "/sys/kernel/debug/fail_page_alloc/%s", name);
	return read_ulong_file(path, value);
}

static int find_kernel_symbol(const char *name, unsigned long *address)
{
	char symbol[256];
	unsigned long value;
	char type;
	FILE *file;
	int found = 0;

	file = fopen("/proc/kallsyms", "r");
	if (!file)
		return -1;
	while (fscanf(file, "%lx %c %255s", &value, &type, symbol) == 3) {
		if (strcmp(symbol, name))
			continue;
		*address = value;
		found = value != 0;
		break;
	}
	fclose(file);
	return found ? 0 : -1;
}

static int expect_vma(unsigned long address, unsigned long expected_start,
		      unsigned long expected_end, const char *name)
{
	unsigned long start = 0;
	unsigned long end = 0;

	if (find_vma_bounds(address, &start, &end)) {
		fprintf(stderr, "COPY_VMA %s: VMA not found\n", name);
		return 1;
	}
	if (start != expected_start || end != expected_end) {
		fprintf(stderr,
			"COPY_VMA %s: expected %#lx-%#lx, found %#lx-%#lx\n",
			name, expected_start, expected_end, start, end);
		return 1;
	}
	return 0;
}

static void *map_fixed(void *address, size_t length)
{
	return mmap(address, length, PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED | MAP_NORESERVE,
		    -1, 0);
}

static int fill_and_check(unsigned char *address, size_t length,
			  unsigned char value, const char *name)
{
	size_t offset;

	for (offset = 0; offset < length; offset += page_size)
		address[offset] = value;

	for (offset = 0; offset < length; offset += page_size) {
		if (address[offset] != value) {
			fprintf(stderr, "COPY_VMA %s: data mismatch at %#zx\n",
				name, offset);
			return 1;
		}
	}
	return 0;
}

static int test_anonymous_move(unsigned char *carveout)
{
	const size_t length = 4 * page_size;
	unsigned char *source = carveout + 8 * page_size;
	unsigned char *target = carveout + 24 * page_size;
	unsigned long before_vm;
	unsigned long after_vm;
	unsigned long source_start;
	unsigned long source_end;
	unsigned char *moved;

	if (map_fixed(source, length) != source)
		goto fail;
	if (fill_and_check(source, length, 0x31, "anonymous setup"))
		return 1;
	if (munmap(target, length))
		goto fail;
	if (read_total_vm_pages(&before_vm))
		goto fail;

	moved = mremap(source, length, length,
		       MREMAP_MAYMOVE | MREMAP_FIXED, target);
	if (moved == MAP_FAILED)
		goto fail;
	if (moved != target) {
		fprintf(stderr, "COPY_VMA anonymous move: returned %p, expected %p\n",
			moved, target);
		return 1;
	}
	if (read_total_vm_pages(&after_vm))
		goto fail;
	if (after_vm != before_vm) {
		fprintf(stderr,
			"COPY_VMA anonymous move changed virtual size from %lu to %lu pages\n",
			before_vm, after_vm);
		return 1;
	}
	if (fill_and_check(target, length, 0x31, "anonymous move"))
		return 1;
	if (expect_vma((unsigned long)target, (unsigned long)target,
		       (unsigned long)(target + length), "anonymous move"))
		return 1;
	if (!find_vma_bounds((unsigned long)source, &source_start,
			     &source_end)) {
		fprintf(stderr,
			"COPY_VMA anonymous move left source VMA mapped\n");
		return 1;
	}

	munmap(target, length);
	return 0;

fail:
	fprintf(stderr, "COPY_VMA anonymous move failed: %s\n", strerror(errno));
	return 1;
}

static int test_dontunmap(unsigned char *carveout)
{
	const size_t length = 3 * page_size;
	unsigned char *source = carveout + 40 * page_size;
	unsigned char *target = carveout + 56 * page_size;
	unsigned long before_vm;
	unsigned long after_vm;
	unsigned long locked;
	unsigned char *moved;

	if (map_fixed(source, length) != source)
		goto fail;
	if (fill_and_check(source, length, 0x42, "DONTUNMAP setup"))
		return 1;
	if (mlock(source, length))
		goto fail;
	if (read_locked_bytes((unsigned long)source, &locked) || !locked) {
		fprintf(stderr, "COPY_VMA DONTUNMAP source is not locked\n");
		return 1;
	}
	if (munmap(target, length))
		goto fail;
	if (read_total_vm_pages(&before_vm))
		goto fail;

	moved = mremap(source, length, length,
		       MREMAP_MAYMOVE | MREMAP_FIXED | MREMAP_DONTUNMAP,
		       target);
	if (moved == MAP_FAILED)
		goto fail;
	if (moved != target) {
		fprintf(stderr, "COPY_VMA DONTUNMAP: returned %p, expected %p\n",
			moved, target);
		return 1;
	}
	if (read_total_vm_pages(&after_vm))
		goto fail;
	if (after_vm != before_vm + length / page_size) {
		fprintf(stderr,
			"COPY_VMA DONTUNMAP virtual size was %lu, expected %lu pages\n",
			after_vm, before_vm + length / page_size);
		return 1;
	}
	if (fill_and_check(target, length, 0x42, "DONTUNMAP target"))
		return 1;
	if (expect_vma((unsigned long)source, (unsigned long)source,
		       (unsigned long)(source + length), "DONTUNMAP source"))
		return 1;
	if (expect_vma((unsigned long)target, (unsigned long)target,
		       (unsigned long)(target + length), "DONTUNMAP target"))
		return 1;
	if (read_locked_bytes((unsigned long)source, &locked) || locked) {
		fprintf(stderr,
			"COPY_VMA DONTUNMAP source VMA remains locked\n");
		return 1;
	}
	if (read_locked_bytes((unsigned long)target, &locked) || !locked) {
		fprintf(stderr,
			"COPY_VMA DONTUNMAP target VMA is not locked\n");
		return 1;
	}

	munmap(source, length);
	munmap(target, length);
	return 0;

fail:
	fprintf(stderr, "COPY_VMA DONTUNMAP failed: %s\n", strerror(errno));
	return 1;
}

static int test_file_move(unsigned char *carveout)
{
	const size_t length = 4 * page_size;
	unsigned char *source = carveout + 72 * page_size;
	unsigned char *target = carveout + 88 * page_size;
	unsigned char *moved;
	int fd;

	fd = memfd_create("copy-vma", 0);
	if (fd < 0)
		goto fail;
	if (ftruncate(fd, length))
		goto fail_fd;

	source = mmap(source, length, PROT_READ | PROT_WRITE,
		      MAP_SHARED | MAP_FIXED, fd, 0);
	if (source == MAP_FAILED)
		goto fail_fd;
	if (fill_and_check(source, length, 0x53, "file setup"))
		return 1;

	moved = mremap(source, length, length,
		       MREMAP_MAYMOVE | MREMAP_FIXED, target);
	if (moved == MAP_FAILED)
		goto fail_fd;
	if (moved != target) {
		fprintf(stderr, "COPY_VMA file move: returned %p, expected %p\n",
			moved, target);
		return 1;
	}
	if (fill_and_check(target, length, 0x53, "file move"))
		return 1;

	munmap(target, length);
	close(fd);
	return 0;

fail_fd:
	close(fd);
fail:
	fprintf(stderr, "COPY_VMA file move failed: %s\n", strerror(errno));
	return 1;
}

static int test_self_merge_move(unsigned char *carveout)
{
	const size_t length = 2 * page_size;
	unsigned char *source = carveout + 104 * page_size;
	unsigned char *target = source + length;
	unsigned char *moved;

	if (map_fixed(source, length) != source)
		goto fail;

	moved = mremap(source, length, length,
		       MREMAP_MAYMOVE | MREMAP_FIXED, target);
	if (moved == MAP_FAILED)
		goto fail;
	if (moved != target) {
		fprintf(stderr, "COPY_VMA self merge: returned %p, expected %p\n",
			moved, target);
		return 1;
	}
	if (expect_vma((unsigned long)target, (unsigned long)target,
		       (unsigned long)(target + length), "self merge"))
		return 1;

	munmap(target, length);
	return 0;

fail:
	fprintf(stderr, "COPY_VMA self merge failed: %s\n", strerror(errno));
	return 1;
}

static int test_dontunmap_no_self_merge(unsigned char *carveout)
{
	const size_t length = 2 * page_size;
	unsigned char *source = carveout + 112 * page_size;
	unsigned char *target = source + length;
	unsigned char *moved;

	if (map_fixed(source, length) != source)
		goto fail;

	moved = mremap(source, length, length,
		       MREMAP_MAYMOVE | MREMAP_FIXED | MREMAP_DONTUNMAP,
		       target);
	if (moved == MAP_FAILED)
		goto fail;
	if (moved != target) {
		fprintf(stderr,
			"COPY_VMA DONTUNMAP no self merge: returned %p, expected %p\n",
			moved, target);
		return 1;
	}
	if (expect_vma((unsigned long)source, (unsigned long)source,
		       (unsigned long)(source + length),
		       "DONTUNMAP no self merge source"))
		return 1;
	if (expect_vma((unsigned long)target, (unsigned long)target,
		       (unsigned long)(target + length),
		       "DONTUNMAP no self merge target"))
		return 1;

	munmap(source, length);
	munmap(target, length);
	return 0;

fail:
	fprintf(stderr, "COPY_VMA DONTUNMAP no self merge failed: %s\n",
		strerror(errno));
	return 1;
}

static int test_merge_move(unsigned char *carveout)
{
	const size_t length = 2 * page_size;
	unsigned char *left = carveout + 120 * page_size;
	unsigned char *target = left + length;
	unsigned char *right = target + length;
	unsigned char *source = carveout + 144 * page_size;
	unsigned char *moved;

	if (map_fixed(left, length) != left)
		goto fail;
	if (map_fixed(right, length) != right)
		goto fail;
	if (map_fixed(source, length) != source)
		goto fail;

	moved = mremap(source, length, length,
		       MREMAP_MAYMOVE | MREMAP_FIXED, target);
	if (moved == MAP_FAILED)
		goto fail;
	if (moved != target) {
		fprintf(stderr, "COPY_VMA merge move: returned %p, expected %p\n",
			moved, target);
		return 1;
	}
	if (expect_vma((unsigned long)target, (unsigned long)left,
		       (unsigned long)(right + length), "merge move"))
		return 1;

	munmap(left, 3 * length);
	return 0;

fail:
	fprintf(stderr, "COPY_VMA merge move failed: %s\n", strerror(errno));
	return 1;
}

static int test_expand_move(unsigned char *carveout)
{
	const size_t old_length = 2 * page_size;
	const size_t new_length = 4 * page_size;
	unsigned char *source = carveout + 160 * page_size;
	unsigned char *target = carveout + 176 * page_size;
	unsigned char *moved;
	size_t offset;

	if (map_fixed(source, old_length) != source)
		goto fail;
	if (fill_and_check(source, old_length, 0x61, "expand setup"))
		return 1;

	moved = mremap(source, old_length, new_length,
		       MREMAP_MAYMOVE | MREMAP_FIXED, target);
	if (moved == MAP_FAILED)
		goto fail;
	if (moved != target) {
		fprintf(stderr, "COPY_VMA expand move: returned %p, expected %p\n",
			moved, target);
		return 1;
	}
	if (fill_and_check(target, old_length, 0x61, "expand move"))
		return 1;
	for (offset = old_length; offset < new_length; offset += page_size) {
		if (target[offset]) {
			fprintf(stderr,
				"COPY_VMA expand move: new page at %#zx is nonzero\n",
				offset);
			return 1;
		}
	}
	if (expect_vma((unsigned long)target, (unsigned long)target,
		       (unsigned long)(target + new_length), "expand move"))
		return 1;

	munmap(target, new_length);
	return 0;

fail:
	fprintf(stderr, "COPY_VMA expand move failed: %s\n", strerror(errno));
	return 1;
}

static int test_shrink_move(unsigned char *carveout)
{
	const size_t old_length = 4 * page_size;
	const size_t new_length = 2 * page_size;
	unsigned char *source = carveout + 192 * page_size;
	unsigned char *target = carveout + 208 * page_size;
	unsigned char *moved;

	if (map_fixed(source, old_length) != source)
		goto fail;
	if (fill_and_check(source, old_length, 0x72, "shrink setup"))
		return 1;

	moved = mremap(source, old_length, new_length,
		       MREMAP_MAYMOVE | MREMAP_FIXED, target);
	if (moved == MAP_FAILED)
		goto fail;
	if (moved != target) {
		fprintf(stderr, "COPY_VMA shrink move: returned %p, expected %p\n",
			moved, target);
		return 1;
	}
	if (fill_and_check(target, new_length, 0x72, "shrink move"))
		return 1;
	if (expect_vma((unsigned long)target, (unsigned long)target,
		       (unsigned long)(target + new_length), "shrink move"))
		return 1;

	munmap(target, new_length);
	return 0;

fail:
	fprintf(stderr, "COPY_VMA shrink move failed: %s\n", strerror(errno));
	return 1;
}

static int test_vdso_move(unsigned char *carveout)
{
	unsigned long source_start;
	unsigned long source_end;
	unsigned char *target = carveout + 240 * page_size;
	size_t length;
	void *moved;

	if (find_named_vma("[vdso]", &source_start, &source_end)) {
		fprintf(stderr, "COPY_VMA vdso move: mapping not found\n");
		return 1;
	}

	length = source_end - source_start;
	moved = mremap((void *)source_start, length, length,
		       MREMAP_MAYMOVE | MREMAP_FIXED, target);
	if (moved == MAP_FAILED)
		goto fail;
	if (moved != target) {
		fprintf(stderr, "COPY_VMA vdso move: returned %p, expected %p\n",
			moved, target);
		return 1;
	}
	if (expect_vma((unsigned long)target, (unsigned long)target,
		       (unsigned long)(target + length), "vdso move"))
		return 1;

	munmap(target, length);
	return 0;

fail:
	fprintf(stderr, "COPY_VMA vdso move failed: %s\n", strerror(errno));
	return 1;
}

static int test_aio_callback_failure(unsigned char *carveout)
{
	const unsigned int nr_events = 256;
	unsigned long source_start = 0;
	unsigned long source_end = 0;
	unsigned long target_start;
	unsigned long target_end;
	unsigned long before_pfn;
	unsigned long after_pfn;
	unsigned char *target = carveout + 256 * page_size;
	aio_context_t context = 0;
	int before_present;
	int after_present;
	int saved_errno;
	void *moved;
	int result = 1;

	if (syscall(SYS_io_setup, nr_events, &context)) {
		fprintf(stderr, "COPY_VMA AIO setup failed: %s\n",
			strerror(errno));
		return 1;
	}
	if (find_named_vma("[aio]", &source_start, &source_end)) {
		fprintf(stderr, "COPY_VMA AIO mapping not found\n");
		goto out;
	}
	if (source_end - source_start <= page_size) {
		fprintf(stderr,
			"COPY_VMA AIO mapping is too small for a partial move\n");
		goto out;
	}

	if (madvise((void *)source_start, page_size, MADV_POPULATE_READ)) {
		fprintf(stderr, "COPY_VMA AIO source population failed: %s\n",
			strerror(errno));
		goto out;
	}
	if (read_pagemap(source_start, &before_present, &before_pfn) ||
	    !before_present) {
		fprintf(stderr, "COPY_VMA AIO source page is not present\n");
		goto out;
	}

	moved = mremap((void *)source_start, page_size, page_size,
		       MREMAP_MAYMOVE | MREMAP_FIXED, target);
	saved_errno = errno;
	if (moved != MAP_FAILED) {
		fprintf(stderr,
			"COPY_VMA AIO callback unexpectedly succeeded at %p\n",
			moved);
		goto out;
	}
	if (saved_errno != EINVAL) {
		fprintf(stderr,
			"COPY_VMA AIO callback failed with %s, expected EINVAL\n",
			strerror(saved_errno));
		goto out;
	}
	if (expect_vma(source_start, source_start, source_end,
		       "AIO callback failure source"))
		goto out;
	if (!find_vma_bounds((unsigned long)target, &target_start,
			     &target_end)) {
		fprintf(stderr,
			"COPY_VMA AIO callback failure left destination mapped\n");
		goto out;
	}
	if (read_pagemap(source_start, &after_present, &after_pfn) ||
	    !after_present || after_pfn != before_pfn) {
		fprintf(stderr,
			"COPY_VMA AIO callback failure did not restore source page\n");
		goto out;
	}

	result = 0;
out:
	if (syscall(SYS_io_destroy, context) && !result) {
		fprintf(stderr, "COPY_VMA AIO destroy failed: %s\n",
			strerror(errno));
		result = 1;
	}
	return result;
}

static int test_partial_move_failure(void)
{
	const unsigned long pgd_span = 1UL << 39;
	const unsigned long source_base = 1UL << 40;
	const unsigned long target_base = 2UL << 40;
	const unsigned long source_page = source_base + page_size;
	const unsigned long last_page = source_base + pgd_span;
	const size_t source_length = pgd_span + 2 * page_size;
	const size_t move_length = pgd_span;
	char value[64];
	unsigned long target_start;
	unsigned long target_end;
	unsigned long symbol;
	unsigned long failures;
	uint64_t before_first;
	uint64_t before_last;
	uint64_t after_first;
	uint64_t after_last;
	void *source = MAP_FAILED;
	void *target = MAP_FAILED;
	void *probe;
	void *moved;
	int saved_errno;
	int armed = 0;
	int result = 1;

	if (setup_debugfs() ||
	    access("/sys/kernel/debug/fail_page_alloc", F_OK) ||
	    access("/proc/self/make-it-fail", F_OK) ||
	    access("/proc/self/clear_refs", F_OK)) {
		fprintf(stderr,
			"COPY_VMA partial move: fault-injection test skipped\n");
		return 0;
	}

	probe = mmap((void *)(1UL << 47), page_size, PROT_NONE,
		     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE |
			     MAP_NORESERVE,
		     -1, 0);
	if (probe != MAP_FAILED) {
		munmap(probe, page_size);
		fprintf(stderr,
			"COPY_VMA partial move: five-level paging is not covered\n");
		return 0;
	}
	if (errno != ENOMEM && errno != EINVAL) {
		fprintf(stderr, "COPY_VMA partial move probe failed: %s\n",
			strerror(errno));
		return 1;
	}

	target = mmap((void *)target_base, move_length, PROT_NONE,
		      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE |
			      MAP_NORESERVE,
		      -1, 0);
	if (target != (void *)target_base)
		goto fail;
	source = mmap((void *)source_base, source_length,
		      PROT_READ | PROT_WRITE,
		      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE |
			      MAP_NORESERVE,
		      -1, 0);
	if (source != (void *)source_base)
		goto fail;

	if (madvise((void *)source_page, page_size, MADV_POPULATE_READ) ||
	    madvise((void *)last_page, page_size, MADV_POPULATE_READ))
		goto fail;
	if (write_string_file("/proc/self/clear_refs", "4\n"))
		goto fail;
	if (read_pagemap_entry(source_page, &before_first) ||
	    read_pagemap_entry(last_page, &before_last) ||
	    !(before_first & (1ULL << 63)) ||
	    !(before_last & (1ULL << 63)))
		goto fail;

	if (find_kernel_symbol("move_page_tables", &symbol))
		goto fail;
	if (write_fail_page_alloc("probability", "0") ||
	    write_fail_page_alloc("verbose", "0") ||
	    write_fail_page_alloc("space", "0") ||
	    write_fail_page_alloc("interval", "5") ||
	    write_fail_page_alloc("times", "1") ||
	    write_fail_page_alloc("task-filter", "Y") ||
	    write_fail_page_alloc("ignore-gfp-wait", "N") ||
	    write_fail_page_alloc("ignore-gfp-highmem", "N") ||
	    write_fail_page_alloc("min-order", "0") ||
	    write_fail_page_alloc("stacktrace-depth", "32"))
		goto fail;

	snprintf(value, sizeof(value), "0x%lx\n", symbol);
	if (write_fail_page_alloc("require-start", value))
		goto fail;
	snprintf(value, sizeof(value), "0x%lx\n", symbol + 0x1000);
	if (write_fail_page_alloc("require-end", value))
		goto fail;

	if (write_string_file("/proc/self/make-it-fail", "1\n"))
		goto fail;
	armed = 1;
	if (write_fail_page_alloc("probability", "100"))
		goto fail;

	moved = mremap((void *)source_page, move_length, move_length,
		       MREMAP_MAYMOVE | MREMAP_FIXED, (void *)target_base);
	saved_errno = errno;

	if (write_fail_page_alloc("probability", "0") ||
	    write_string_file("/proc/self/make-it-fail", "0\n")) {
		armed = 0;
		goto fail;
	}
	armed = 0;

	if (read_fail_page_alloc("times", &failures))
		goto fail;
	if (failures) {
		fprintf(stderr,
			"COPY_VMA partial move injection did not fire: moved=%p errno=%s\n",
			moved, strerror(saved_errno));
		goto out;
	}
	if (moved != MAP_FAILED) {
		fprintf(stderr,
			"COPY_VMA partial move unexpectedly succeeded at %p\n",
			moved);
		goto out;
	}
	if (saved_errno != ENOMEM) {
		fprintf(stderr,
			"COPY_VMA partial move failed with %s, expected ENOMEM\n",
			strerror(saved_errno));
		goto out;
	}
	if (expect_vma(source_page, source_base, source_base + source_length,
		       "partial move source"))
		goto out;
	if (!find_vma_bounds(target_base, &target_start, &target_end)) {
		fprintf(stderr,
			"COPY_VMA partial move left destination mapped\n");
		goto out;
	}
	if (read_pagemap_entry(source_page, &after_first) ||
	    read_pagemap_entry(last_page, &after_last))
		goto out;
	if (!(after_first & (1ULL << 63)) ||
	    (after_first & ((1ULL << 55) - 1)) !=
		    (before_first & ((1ULL << 55) - 1)) ||
	    !(after_last & (1ULL << 63)) ||
	    (after_last & ((1ULL << 55) - 1)) !=
		    (before_last & ((1ULL << 55) - 1))) {
		fprintf(stderr,
			"COPY_VMA partial move did not restore source pages\n");
		goto out;
	}
	if (!(after_first & (1ULL << 55)) || (after_last & (1ULL << 55))) {
		fprintf(stderr,
			"COPY_VMA partial move stopped at the wrong page\n");
		goto out;
	}

	fprintf(stderr, "COPY_VMA partial move: PASS\n");
	result = 0;
	goto out;

fail:
	fprintf(stderr, "COPY_VMA partial move setup failed: %s\n",
		strerror(errno));
out:
	if (armed) {
		write_fail_page_alloc("probability", "0");
		write_string_file("/proc/self/make-it-fail", "0\n");
	}
	if (source != MAP_FAILED)
		munmap(source, source_length);
	if (target != MAP_FAILED)
		munmap(target, move_length);
	return result;
}

int main(void)
{
	unsigned char *carveout;
	size_t length;
	int result = 1;

	setup_console();
	if (setup_proc()) {
		fprintf(stderr, "COPY_VMA proc setup failed: %s\n",
			strerror(errno));
		goto out;
	}

	page_size = sysconf(_SC_PAGESIZE);
	if ((long)page_size <= 0)
		goto out;

	length = CARVEOUT_PAGES * page_size;
	carveout = mmap(NULL, length, PROT_NONE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (carveout == MAP_FAILED)
		goto out;

	if (test_anonymous_move(carveout) ||
	    test_dontunmap(carveout) ||
	    test_file_move(carveout) ||
	    test_self_merge_move(carveout) ||
	    test_dontunmap_no_self_merge(carveout) ||
	    test_merge_move(carveout) ||
	    test_expand_move(carveout) ||
	    test_shrink_move(carveout) ||
	    test_vdso_move(carveout) ||
	    test_aio_callback_failure(carveout) ||
	    test_partial_move_failure())
		goto out;

	result = 0;
out:
	report_result(!result);
	if (!iopl(3))
		outb(result ? QEMU_EXIT_FAILURE : QEMU_EXIT_SUCCESS, QEMU_EXIT_PORT);
	reboot(RB_POWER_OFF);
	return result;
}
