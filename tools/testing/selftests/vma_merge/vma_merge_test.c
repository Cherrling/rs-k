// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <linux/prctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/io.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define CARVEOUT_PAGES	240
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
		"vma_merge userspace: PASS\n" :
		"vma_merge userspace: FAIL\n";

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

static int expect_one_vma(unsigned long start, unsigned long end,
			  const char *name)
{
	unsigned long found_start = 0;
	unsigned long found_end = 0;

	if (find_vma_bounds(start, &found_start, &found_end)) {
		fprintf(stderr, "VMA_MERGE %s: VMA not found\n", name);
		return 1;
	}
	if (found_start != start || found_end != end) {
		fprintf(stderr,
			"VMA_MERGE %s: expected %#lx-%#lx, found %#lx-%#lx\n",
			name, start, end, found_start, found_end);
		return 1;
	}
	return 0;
}

static int expect_two_vmas(unsigned long start, unsigned long middle,
			   unsigned long end, const char *name)
{
	unsigned long found_start = 0;
	unsigned long found_end = 0;

	if (find_vma_bounds(start, &found_start, &found_end)) {
		fprintf(stderr, "VMA_MERGE %s: first VMA not found\n", name);
		return 1;
	}
	if (found_start != start || found_end != middle) {
		fprintf(stderr,
			"VMA_MERGE %s: first VMA expected %#lx-%#lx, found %#lx-%#lx\n",
			name, start, middle, found_start, found_end);
		return 1;
	}
	if (find_vma_bounds(middle, &found_start, &found_end)) {
		fprintf(stderr, "VMA_MERGE %s: second VMA not found\n", name);
		return 1;
	}
	if (found_start != middle || found_end != end) {
		fprintf(stderr,
			"VMA_MERGE %s: second VMA expected %#lx-%#lx, found %#lx-%#lx\n",
			name, middle, end, found_start, found_end);
		return 1;
	}
	return 0;
}

static void *map_fixed(void *address, size_t length, int protection)
{
	return mmap(address, length, protection,
		    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED | MAP_NORESERVE,
		    -1, 0);
}

static void *map_file_fixed(void *address, size_t length, int fd,
			    off_t offset)
{
	return mmap(address, length, PROT_READ | PROT_WRITE,
		    MAP_SHARED | MAP_FIXED, fd, offset);
}

static void *map_private_file_fixed(void *address, size_t length, int fd,
				    off_t offset)
{
	return mmap(address, length, PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_FIXED, fd, offset);
}

static int create_memfd(size_t size)
{
	int fd = memfd_create("vma-merge", 0);

	if (fd < 0)
		return -1;
	if (ftruncate(fd, size)) {
		close(fd);
		return -1;
	}
	return fd;
}

static void fault_mapping(unsigned char *address, size_t length)
{
	memset(address, 0x5a, length);
}

static int name_vma(void *address, size_t length, const char *name)
{
	return prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME,
		     (unsigned long)address, (unsigned long)length,
		     (unsigned long)name);
}

static int test_new_range_merges(unsigned char *carveout)
{
	const size_t length = 5 * page_size;
	unsigned char *left = carveout;
	unsigned char *left_target = carveout + 17 * page_size;
	unsigned char *left_new = carveout + 12 * page_size;
	unsigned char *middle = carveout + 31 * page_size;
	unsigned char *right = carveout + 36 * page_size;
	unsigned char *pair = carveout + 26 * page_size;
	int result = 1;

	if (map_fixed(left, length, PROT_READ | PROT_WRITE) != left)
		goto fail;
	if (map_fixed(left + length, length, PROT_READ | PROT_WRITE) !=
	    left + length)
		goto fail;
	if (expect_one_vma((unsigned long)left, (unsigned long)(left + 2 * length),
			   "right adjacency"))
		goto out;

	if (map_fixed(left_target, length, PROT_READ | PROT_WRITE) != left_target)
		goto fail;
	if (map_fixed(left_new, length, PROT_READ | PROT_WRITE) != left_new)
		goto fail;
	if (expect_one_vma((unsigned long)left_new,
			   (unsigned long)(left_target + length),
			   "left adjacency"))
		goto out;

	if (map_fixed(pair, length, PROT_READ | PROT_WRITE) != pair)
		goto fail;
	if (map_fixed(right, length, PROT_READ | PROT_WRITE) != right)
		goto fail;
	if (map_fixed(middle, length, PROT_READ | PROT_WRITE) != middle)
		goto fail;
	if (expect_one_vma((unsigned long)pair, (unsigned long)(right + length),
			   "both adjacencies"))
		goto out;

	result = 0;
	goto out;

fail:
	fprintf(stderr, "VMA_MERGE mmap failed: %s\n", strerror(errno));
out:
	return result;
}

static int test_incompatible_ranges(unsigned char *carveout)
{
	unsigned char *rw = carveout + 46 * page_size;
	unsigned char *ro = rw + 5 * page_size;
	const size_t length = 5 * page_size;

	if (map_fixed(rw, length, PROT_READ | PROT_WRITE) != rw)
		goto fail;
	if (map_fixed(ro, length, PROT_READ) != ro)
		goto fail;
	return expect_two_vmas((unsigned long)rw, (unsigned long)ro,
			       (unsigned long)(ro + length), "different flags");

fail:
	fprintf(stderr, "VMA_MERGE fixed mapping failed: %s\n", strerror(errno));
	return 1;
}

static int test_file_mappings(unsigned char *carveout)
{
	const size_t length = 4 * page_size;
	unsigned char *contiguous = carveout + 72 * page_size;
	unsigned char *disjoint = contiguous + length;
	unsigned char *identity = contiguous + 3 * length;
	unsigned char *identity_second = identity + length;
	unsigned char *existing = carveout + 108 * page_size;
	unsigned char *existing_disjoint = carveout + 120 * page_size;
	unsigned char *private_first = carveout + 228 * page_size;
	int first_fd = -1;
	int second_fd = -1;
	int result = 1;

	first_fd = create_memfd(32 * page_size);
	second_fd = create_memfd(32 * page_size);
	if (first_fd < 0 || second_fd < 0)
		goto fail;

	if (map_file_fixed(contiguous, length, first_fd, 0) != contiguous)
		goto fail;
	if (map_file_fixed(contiguous + length, length, first_fd,
			   (off_t)length) != contiguous + length)
		goto fail;
	if (expect_one_vma((unsigned long)contiguous,
			   (unsigned long)(contiguous + 2 * length),
			   "contiguous file offsets"))
		goto out;

	if (map_file_fixed(disjoint, length, first_fd,
			   (off_t)(12 * page_size)) != disjoint)
		goto fail;
	if (expect_two_vmas((unsigned long)contiguous,
			    (unsigned long)disjoint,
			    (unsigned long)(disjoint + length),
			    "disjoint file offsets"))
		goto out;

	if (map_file_fixed(identity, length, first_fd, 0) != identity)
		goto fail;
	if (map_file_fixed(identity_second, length, second_fd,
			   (off_t)length) != identity_second)
		goto fail;
	if (expect_two_vmas((unsigned long)identity,
			    (unsigned long)identity_second,
			    (unsigned long)(identity_second + length),
			    "different files"))
		goto out;

	if (map_file_fixed(existing, length, first_fd,
			   (off_t)(8 * page_size)) != existing)
		goto fail;
	if (mprotect(existing, length, PROT_READ))
		goto fail;
	if (map_file_fixed(existing + length, length, first_fd,
			   (off_t)(12 * page_size)) != existing + length)
		goto fail;
	if (mprotect(existing, length, PROT_READ | PROT_WRITE))
		goto fail;
	if (expect_one_vma((unsigned long)existing,
			   (unsigned long)(existing + 2 * length),
			   "existing file offsets"))
		goto out;

	if (map_file_fixed(existing_disjoint, length, first_fd,
			   (off_t)(16 * page_size)) != existing_disjoint)
		goto fail;
	if (mprotect(existing_disjoint, length, PROT_READ))
		goto fail;
	if (map_file_fixed(existing_disjoint + length, length, first_fd,
			   (off_t)(28 * page_size)) !=
	    existing_disjoint + length)
		goto fail;
	if (mprotect(existing_disjoint, length, PROT_READ | PROT_WRITE))
		goto fail;
	if (expect_two_vmas((unsigned long)existing_disjoint,
			    (unsigned long)(existing_disjoint + length),
			    (unsigned long)(existing_disjoint + 2 * length),
			    "existing disjoint file offsets"))
		goto out;

	if (map_private_file_fixed(private_first, length, first_fd,
				   (off_t)(20 * page_size)) != private_first)
		goto fail;
	fault_mapping(private_first, length);
	if (mprotect(private_first, length, PROT_READ))
		goto fail;
	if (map_private_file_fixed(private_first + length, length, first_fd,
				   (off_t)(24 * page_size)) !=
	    private_first + length)
		goto fail;
	if (mprotect(private_first, length, PROT_READ | PROT_WRITE))
		goto fail;
	if (expect_one_vma((unsigned long)private_first,
			   (unsigned long)(private_first + 2 * length),
			   "existing private file merge"))
		goto out;

	result = 0;
	goto out;

fail:
	fprintf(stderr, "VMA_MERGE file mapping failed: %s\n", strerror(errno));
out:
	if (first_fd >= 0)
		close(first_fd);
	if (second_fd >= 0)
		close(second_fd);
	return result;
}

static int test_existing_range_merges(unsigned char *carveout)
{
	const size_t length = 4 * page_size;
	unsigned char *right = carveout + 128 * page_size;
	unsigned char *left_prev = carveout + 140 * page_size;
	unsigned char *left = left_prev + length;
	unsigned char *both_prev = carveout + 152 * page_size;
	unsigned char *both = both_prev + length;
	unsigned char *both_next = both + length;
	unsigned char *shrink_left_prev = carveout + 168 * page_size;
	unsigned char *shrink_left = shrink_left_prev + length;
	unsigned char *shrink_right = carveout + 184 * page_size;
	unsigned char *shrink_right_next = shrink_right + 2 * length;
	unsigned char *anon_left_prev = carveout + 200 * page_size;
	unsigned char *anon_left = anon_left_prev + length;
	unsigned char *anon_right = carveout + 212 * page_size;
	unsigned char *anon_right_next = anon_right + length;

	if (map_fixed(right, length, PROT_READ) != right)
		goto fail;
	if (map_fixed(right + length, length, PROT_READ | PROT_WRITE) !=
	    right + length)
		goto fail;
	if (mprotect(right, length, PROT_READ | PROT_WRITE))
		goto fail;
	if (expect_one_vma((unsigned long)right,
			   (unsigned long)(right + 2 * length),
			   "existing right merge"))
		goto out;

	if (map_fixed(left_prev, length, PROT_READ | PROT_WRITE) != left_prev)
		goto fail;
	if (map_fixed(left, length, PROT_READ) != left)
		goto fail;
	if (mprotect(left, length, PROT_READ | PROT_WRITE))
		goto fail;
	if (expect_one_vma((unsigned long)left_prev,
			   (unsigned long)(left + length),
			   "existing left merge"))
		goto out;

	if (map_fixed(both_prev, length, PROT_READ | PROT_WRITE) != both_prev)
		goto fail;
	if (map_fixed(both, length, PROT_READ) != both)
		goto fail;
	if (map_fixed(both_next, length, PROT_READ | PROT_WRITE) != both_next)
		goto fail;
	if (mprotect(both, length, PROT_READ | PROT_WRITE))
		goto fail;
	if (expect_one_vma((unsigned long)both_prev,
			   (unsigned long)(both_next + length),
			   "existing two-sided merge"))
		goto out;

	if (map_fixed(shrink_left_prev, length, PROT_READ | PROT_WRITE) !=
	    shrink_left_prev)
		goto fail;
	if (map_fixed(shrink_left, 2 * length, PROT_READ) != shrink_left)
		goto fail;
	if (mprotect(shrink_left, length, PROT_READ | PROT_WRITE))
		goto fail;
	if (expect_two_vmas((unsigned long)shrink_left_prev,
			    (unsigned long)(shrink_left + length),
			    (unsigned long)(shrink_left + 2 * length),
			    "existing left shrink"))
		goto out;

	if (map_fixed(shrink_right, 2 * length, PROT_READ) != shrink_right)
		goto fail;
	if (map_fixed(shrink_right_next, length, PROT_READ | PROT_WRITE) !=
	    shrink_right_next)
		goto fail;
	if (mprotect(shrink_right + length, length, PROT_READ | PROT_WRITE))
		goto fail;
	if (expect_two_vmas((unsigned long)shrink_right,
			    (unsigned long)(shrink_right + length),
			    (unsigned long)(shrink_right_next + length),
			    "existing right shrink"))
		goto out;

	if (map_fixed(anon_left, length, PROT_READ | PROT_WRITE) != anon_left)
		goto fail;
	fault_mapping(anon_left, length);
	if (mprotect(anon_left, length, PROT_READ))
		goto fail;
	if (map_fixed(anon_left_prev, length, PROT_READ | PROT_WRITE) !=
	    anon_left_prev)
		goto fail;
	if (mprotect(anon_left, length, PROT_READ | PROT_WRITE))
		goto fail;
	if (expect_one_vma((unsigned long)anon_left_prev,
			   (unsigned long)(anon_left + length),
			   "existing left anon_vma copy"))
		goto out;

	if (map_fixed(anon_right, length, PROT_READ | PROT_WRITE) != anon_right)
		goto fail;
	fault_mapping(anon_right, length);
	if (mprotect(anon_right, length, PROT_READ))
		goto fail;
	if (map_fixed(anon_right_next, length, PROT_READ | PROT_WRITE) !=
	    anon_right_next)
		goto fail;
	if (mprotect(anon_right, length, PROT_READ | PROT_WRITE))
		goto fail;
	if (expect_one_vma((unsigned long)anon_right,
			   (unsigned long)(anon_right_next + length),
			   "existing right anon_vma copy"))
		goto out;

	return 0;

fail:
	fprintf(stderr, "VMA_MERGE existing-range mapping failed: %s\n",
		strerror(errno));
out:
	return 1;
}

static int test_anon_names(unsigned char *carveout)
{
	const size_t length = 4 * page_size;
	unsigned char *same_first = carveout + 92 * page_size;
	unsigned char *same_second = same_first + length;
	unsigned char *different_first = carveout + 100 * page_size;
	unsigned char *different_second = different_first + length;

	if (map_fixed(same_first, length, PROT_READ | PROT_WRITE) != same_first)
		goto fail;
	if (name_vma(same_first, length, "shared")) {
		if (errno == EINVAL)
			return 0;
		goto fail;
	}
	if (map_fixed(same_second, length, PROT_READ) != same_second)
		goto fail;
	if (name_vma(same_second, length, "shared"))
		goto fail;
	if (mprotect(same_second, length, PROT_READ | PROT_WRITE))
		goto fail;
	if (expect_one_vma((unsigned long)same_first,
			   (unsigned long)(same_second + length),
			   "same anonymous name"))
		return 1;

	if (map_fixed(different_first, length,
		      PROT_READ | PROT_WRITE) != different_first)
		goto fail;
	if (name_vma(different_first, length, "left"))
		goto fail;
	if (map_fixed(different_second, length, PROT_READ) != different_second)
		goto fail;
	if (name_vma(different_second, length, "right"))
		goto fail;
	if (mprotect(different_second, length, PROT_READ | PROT_WRITE))
		goto fail;
	return expect_two_vmas((unsigned long)different_first,
			       (unsigned long)different_second,
			       (unsigned long)(different_second + length),
			       "different anonymous names");

fail:
	fprintf(stderr, "VMA_MERGE anonymous name failed: %s\n",
		strerror(errno));
	return 1;
}

static int child_forked_target(unsigned char *carveout)
{
	unsigned char *first = carveout + 60 * page_size;
	unsigned char *second = first + 5 * page_size;
	const size_t length = 5 * page_size;
	size_t offset;

	for (offset = 0; offset < length; offset += page_size)
		first[offset] = 2;

	if (map_fixed(second, length, PROT_READ | PROT_WRITE) != second)
		return 1;
	return expect_two_vmas((unsigned long)first, (unsigned long)second,
			       (unsigned long)(second + length),
			       "forked target");
}

static int test_forked_target(unsigned char *carveout)
{
	unsigned char *first = carveout + 60 * page_size;
	const size_t length = 5 * page_size;
	pid_t pid;
	int status;
	size_t offset;

	if (map_fixed(first, length, PROT_READ | PROT_WRITE) != first)
		return 1;
	for (offset = 0; offset < length; offset += page_size)
		first[offset] = 1;

	pid = fork();
	if (pid < 0)
		return 1;
	if (!pid)
		_exit(child_forked_target(carveout));

	if (waitpid(pid, &status, 0) != pid)
		return 1;
	if (!WIFEXITED(status) || WEXITSTATUS(status)) {
		fprintf(stderr, "VMA_MERGE forked target failed\n");
		return 1;
	}
	return 0;
}

static int test_brk_expand(void)
{
	unsigned char *current = sbrk(0);
	unsigned char *next;

	if (current == (void *)-1)
		return 1;
	next = current + 8 * page_size;
	if (brk(next))
		return 1;
	*current = 1;
	return 0;
}

int main(void)
{
	unsigned char *carveout;
	size_t length;
	int result = 1;

	setup_console();
	if (setup_proc()) {
		fprintf(stderr, "VMA_MERGE proc setup failed: %s\n",
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

	if (test_new_range_merges(carveout) ||
	    test_incompatible_ranges(carveout) ||
	    test_file_mappings(carveout) ||
	    test_existing_range_merges(carveout) ||
	    test_anon_names(carveout) ||
	    test_forked_target(carveout) ||
	    test_brk_expand())
		goto out;

	result = 0;
out:
	report_result(!result);
	if (!iopl(3))
		outb(result ? QEMU_EXIT_FAILURE : QEMU_EXIT_SUCCESS, QEMU_EXIT_PORT);
	reboot(RB_POWER_OFF);
	return result;
}
