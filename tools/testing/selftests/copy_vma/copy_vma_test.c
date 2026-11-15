// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/io.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
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
	unsigned char *moved;

	if (map_fixed(source, length) != source)
		goto fail;
	if (fill_and_check(source, length, 0x31, "anonymous setup"))
		return 1;

	moved = mremap(source, length, length,
		       MREMAP_MAYMOVE | MREMAP_FIXED, target);
	if (moved == MAP_FAILED)
		goto fail;
	if (moved != target) {
		fprintf(stderr, "COPY_VMA anonymous move: returned %p, expected %p\n",
			moved, target);
		return 1;
	}
	if (fill_and_check(target, length, 0x31, "anonymous move"))
		return 1;
	if (expect_vma((unsigned long)target, (unsigned long)target,
		       (unsigned long)(target + length), "anonymous move"))
		return 1;

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
	unsigned char *moved;

	if (map_fixed(source, length) != source)
		goto fail;
	if (fill_and_check(source, length, 0x42, "DONTUNMAP setup"))
		return 1;

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
	if (fill_and_check(target, length, 0x42, "DONTUNMAP target"))
		return 1;
	if (expect_vma((unsigned long)source, (unsigned long)source,
		       (unsigned long)(source + length), "DONTUNMAP source"))
		return 1;

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
	    test_merge_move(carveout))
		goto out;

	result = 0;
out:
	report_result(!result);
	if (!iopl(3))
		outb(result ? QEMU_EXIT_FAILURE : QEMU_EXIT_SUCCESS, QEMU_EXIT_PORT);
	reboot(RB_POWER_OFF);
	return result;
}
