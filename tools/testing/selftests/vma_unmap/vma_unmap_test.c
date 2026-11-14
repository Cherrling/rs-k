// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define UNMAP_ROUNDS	64
#define UNMAP_PAGES	16
#define UNMAP_WORKERS	4
#define UNMAP_WORKER_ROUNDS	16

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

static void report_result(int passed)
{
	const char *result = passed ?
		"vma_unmap userspace: PASS\n" :
		"vma_unmap userspace: FAIL\n";
	int kmsg = open("/dev/kmsg", O_WRONLY);

	if (kmsg >= 0) {
		write(kmsg, result, strlen(result));
		close(kmsg);
	}
	write(STDOUT_FILENO, result, strlen(result));
}

static int mapping_present(void *address, size_t page_size)
{
	unsigned char vec;
	int attempt;

	for (attempt = 0; attempt < 8; attempt++) {
		if (!mincore(address, page_size, &vec))
			return 1;
		if (errno == ENOMEM)
			return 0;
		if (errno != EAGAIN)
			return -1;
		usleep(1000);
	}
	return -1;
}

static int check_present(void *address, size_t size, size_t page_size, int round)
{
	size_t offset;

	for (offset = 0; offset < size; offset += page_size) {
		int present = mapping_present((char *)address + offset, page_size);

		if (present != 1) {
			fprintf(stderr,
				"VMA_UNMAP expected page %zu present in round %d: %d errno %d\n",
				offset / page_size, round, present, errno);
			return 1;
		}
	}
	return 0;
}

static int check_absent(void *address, size_t size, size_t page_size, int round)
{
	size_t offset;

	for (offset = 0; offset < size; offset += page_size) {
		int present = mapping_present((char *)address + offset, page_size);

		if (present != 0) {
			fprintf(stderr,
				"VMA_UNMAP expected page %zu absent in round %d: %d errno %d\n",
				offset / page_size, round, present, errno);
			return 1;
		}
	}
	return 0;
}

static int test_anonymous_range(size_t page_size, int round)
{
	const size_t size = UNMAP_PAGES * page_size;
	unsigned char *address;
	size_t index;

	address = mmap(NULL, size, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (address == MAP_FAILED) {
		fprintf(stderr, "VMA_UNMAP anon mmap failed in round %d: %s\n",
			round, strerror(errno));
		return 1;
	}

	for (index = 0; index < UNMAP_PAGES; index++)
		address[index * page_size] = index + 1;

	if (munmap(address + 4 * page_size, 4 * page_size) ||
	    munmap(address + 4 * page_size, 4 * page_size)) {
		fprintf(stderr, "VMA_UNMAP middle unmap failed in round %d: %s\n",
			round, strerror(errno));
		goto fail;
	}
	if (check_present(address, 4 * page_size, page_size, round) ||
	    check_absent(address + 4 * page_size, 4 * page_size, page_size,
			 round) ||
	    check_present(address + 8 * page_size, 4 * page_size, page_size,
			  round))
		goto fail;
	if (address[2 * page_size] != 3 || address[8 * page_size] != 9) {
		fprintf(stderr, "VMA_UNMAP value mismatch in round %d\n", round);
		goto fail;
	}

	if (munmap(address, page_size + 1)) {
		fprintf(stderr, "VMA_UNMAP aligned-length unmap failed in round %d: %s\n",
			round, strerror(errno));
		goto fail;
	}
	if (check_absent(address, 2 * page_size, page_size, round))
		goto fail;

	if (munmap(address + 14 * page_size, 2 * page_size)) {
		fprintf(stderr, "VMA_UNMAP tail unmap failed in round %d: %s\n",
			round, strerror(errno));
		goto fail;
	}
	if (check_absent(address + 14 * page_size, 2 * page_size, page_size,
			 round))
		goto fail;

	if (munmap(address, size)) {
		fprintf(stderr, "VMA_UNMAP whole-range unmap failed in round %d: %s\n",
			round, strerror(errno));
		goto fail;
	}
	return 0;

fail:
	munmap(address, size);
	return 1;
}

static int test_invalid_ranges(size_t page_size, int round)
{
	unsigned char *address;

	address = mmap(NULL, 2 * page_size, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (address == MAP_FAILED)
		return 1;

	errno = 0;
	if (munmap(address + 1, page_size) != -1 || errno != EINVAL) {
		fprintf(stderr, "VMA_UNMAP unaligned start accepted in round %d\n",
			round);
		goto fail;
	}
	if (check_present(address, 2 * page_size, page_size, round))
		goto fail;

	errno = 0;
	if (munmap(address, 0) != -1 || errno != EINVAL) {
		fprintf(stderr, "VMA_UNMAP zero length accepted in round %d\n",
			round);
		goto fail;
	}

	errno = 0;
	if (munmap(address, SIZE_MAX) != -1 || errno != EINVAL) {
		fprintf(stderr, "VMA_UNMAP overflow length accepted in round %d\n",
			round);
		goto fail;
	}
	if (munmap(address, 2 * page_size))
		return 1;
	return 0;

fail:
	munmap(address, 2 * page_size);
	return 1;
}

static int test_file_range(size_t page_size, int round)
{
	unsigned char *address;
	void *shared = MAP_FAILED;
	int file_fd;

	file_fd = memfd_create("vma-unmap", 0);
	if (file_fd < 0)
		return 1;
	if (ftruncate(file_fd, UNMAP_PAGES * page_size)) {
		close(file_fd);
		return 1;
	}

	address = mmap(NULL, UNMAP_PAGES * page_size, PROT_READ | PROT_WRITE,
		       MAP_SHARED, file_fd, 0);
	if (address == MAP_FAILED) {
		close(file_fd);
		return 1;
	}
	memset(address, 0x5a, UNMAP_PAGES * page_size);
	if (munmap(address + 3 * page_size, 5 * page_size))
		goto fail;
	if (check_present(address, 3 * page_size, page_size, round) ||
	    check_absent(address + 3 * page_size, 5 * page_size, page_size,
			 round) ||
	    check_present(address + 8 * page_size, 8 * page_size, page_size,
			  round))
		goto fail;

	shared = mmap(NULL, 4 * page_size, PROT_READ | PROT_WRITE,
		      MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (shared == MAP_FAILED)
		goto fail;
	memset(shared, 0xa5, 4 * page_size);
	if (munmap(shared, 4 * page_size))
		goto fail;

	if (munmap(address, UNMAP_PAGES * page_size))
		goto fail;
	close(file_fd);
	return 0;

fail:
	munmap(address, UNMAP_PAGES * page_size);
	if (shared != MAP_FAILED)
		munmap(shared, 4 * page_size);
	close(file_fd);
	return 1;
}

static int test_fixed_overlap(size_t page_size, int round)
{
	const size_t size = UNMAP_PAGES * page_size;
	const size_t replacement = 8 * page_size;
	unsigned char *address;
	void *mapped;
	size_t index;

	address = mmap(NULL, size, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (address == MAP_FAILED)
		return 1;
	for (index = 0; index < UNMAP_PAGES; index++)
		address[index * page_size] = index + 1;

	mapped = mmap(address + 3 * page_size, replacement, PROT_READ,
		      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	if (mapped != address + 3 * page_size)
		goto fail;
	if (check_present(address, size, page_size, round))
		goto fail;
	if (address[0] != 1 || address[2 * page_size] != 3 ||
	    address[11 * page_size] != 12 ||
	    address[15 * page_size] != 16) {
		fprintf(stderr, "VMA_UNMAP fixed overlap changed neighbors in round %d\n",
			round);
		goto fail;
	}
	if (munmap(address, size))
		goto fail;
	return 0;

fail:
	fprintf(stderr, "VMA_UNMAP fixed overlap failed in round %d: %s\n",
		round, strerror(errno));
	munmap(address, size);
	return 1;
}

static int test_brk_shrink(size_t page_size, int round)
{
	const size_t size = UNMAP_PAGES * page_size;
	char *original = sbrk(0);
	char *base;
	size_t index;

	if (original == (void *)-1)
		return 1;
	base = (char *)(((uintptr_t)original + page_size - 1) &
			~(page_size - 1));
	if (brk(base) || brk(base + size))
		goto fail;
	for (index = 0; index < UNMAP_PAGES; index++)
		base[index * page_size] = index + 1;

	if (brk(base + 3 * page_size))
		goto fail;
	if (check_absent(base + 3 * page_size, size - 3 * page_size,
			 page_size, round))
		goto fail;
	if (brk(base))
		goto fail;
	if (check_absent(base, 3 * page_size, page_size, round))
		goto fail;
	if (brk(original))
		return 1;
	return 0;

fail:
	fprintf(stderr, "VMA_UNMAP brk shrink failed in round %d: %s\n",
		round, strerror(errno));
	brk(base);
	brk(original);
	return 1;
}

static int test_sysv_shm(size_t page_size, int round)
{
	const size_t size = UNMAP_PAGES * page_size;
	unsigned char *address = (void *)-1;
	void *mapped;
	size_t index;
	int shmid;

	shmid = shmget(IPC_PRIVATE, size, IPC_CREAT | 0600);
	if (shmid < 0)
		return 1;
	address = shmat(shmid, NULL, 0);
	if (address == (void *)-1)
		goto fail;
	for (index = 0; index < UNMAP_PAGES; index++)
		address[index * page_size] = index + 1;

	mapped = address;
	if (shmdt(address))
		goto fail;
	address = (void *)-1;
	if (check_absent(mapped, size, page_size, round))
		goto fail;
	if (shmctl(shmid, IPC_RMID, NULL))
		return 1;
	return 0;

fail:
	fprintf(stderr, "VMA_UNMAP SysV shared memory failed in round %d: %s\n",
		round, strerror(errno));
	if (address != (void *)-1)
		shmdt(address);
	shmctl(shmid, IPC_RMID, NULL);
	return 1;
}

struct unmap_gate {
	pthread_mutex_t lock;
	pthread_cond_t cond;
	bool open;
	bool abort;
};

struct unmap_worker {
	struct unmap_gate *gate;
	size_t page_size;
	int round;
	int worker;
	int result;
};

static int test_concurrent_range(size_t page_size, int round, int worker)
{
	const size_t size = UNMAP_PAGES * page_size;
	unsigned char *address;
	int iteration;
	size_t index;

	for (iteration = 0; iteration < UNMAP_WORKER_ROUNDS; iteration++) {
		address = mmap(NULL, size, PROT_READ | PROT_WRITE,
			       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (address == MAP_FAILED) {
			fprintf(stderr,
				"VMA_UNMAP concurrent mmap failed in round %d worker %d\n",
				round, worker);
			return 1;
		}

		for (index = 0; index < UNMAP_PAGES; index++)
			address[index * page_size] = worker + 1;

		if (munmap(address + 4 * page_size, 8 * page_size) ||
		    munmap(address, size)) {
			fprintf(stderr,
				"VMA_UNMAP concurrent unmap failed in round %d worker %d\n",
				round, worker);
			munmap(address, size);
			return 1;
		}
	}
	return 0;
}

static void *run_unmap_worker(void *opaque)
{
	struct unmap_worker *worker = opaque;
	bool abort;

	pthread_mutex_lock(&worker->gate->lock);
	while (!worker->gate->open)
		pthread_cond_wait(&worker->gate->cond, &worker->gate->lock);
	abort = worker->gate->abort;
	pthread_mutex_unlock(&worker->gate->lock);
	if (abort)
		return NULL;
	worker->result = test_concurrent_range(worker->page_size,
					       worker->round,
					       worker->worker);
	return NULL;
}

static int test_concurrent_ranges(size_t page_size, int round)
{
	struct unmap_gate gate = {
		.lock = PTHREAD_MUTEX_INITIALIZER,
		.cond = PTHREAD_COND_INITIALIZER,
	};
	struct unmap_worker workers[UNMAP_WORKERS];
	pthread_t threads[UNMAP_WORKERS];
	int created = 0;
	int result = 0;
	int index;

	for (index = 0; index < UNMAP_WORKERS; index++) {
		workers[index].gate = &gate;
		workers[index].page_size = page_size;
		workers[index].round = round;
		workers[index].worker = index;
		workers[index].result = 1;
		if (pthread_create(&threads[index], NULL, run_unmap_worker,
				   &workers[index])) {
			fprintf(stderr,
				"VMA_UNMAP pthread_create failed in round %d worker %d\n",
				round, index);
			result = 1;
			break;
		}
		created++;
	}

	pthread_mutex_lock(&gate.lock);
	gate.open = true;
	gate.abort = created != UNMAP_WORKERS;
	pthread_cond_broadcast(&gate.cond);
	pthread_mutex_unlock(&gate.lock);

	for (index = 0; index < created; index++)
		if (pthread_join(threads[index], NULL))
			result = 1;

	pthread_cond_destroy(&gate.cond);
	pthread_mutex_destroy(&gate.lock);

	for (index = 0; index < created; index++)
		if (workers[index].result)
			result = 1;

	return result;
}

static int test_unmap_ranges(size_t page_size)
{
	int round;

	for (round = 0; round < UNMAP_ROUNDS; round++) {
		if (test_anonymous_range(page_size, round) ||
		    test_invalid_ranges(page_size, round) ||
		    test_file_range(page_size, round) ||
		    test_fixed_overlap(page_size, round) ||
		    test_brk_shrink(page_size, round) ||
		    test_sysv_shm(page_size, round) ||
		    test_concurrent_ranges(page_size, round))
			return 1;
	}
	return 0;
}

static void power_off(void)
{
	sync();
	reboot(RB_POWER_OFF);
	_exit(0);
}

int main(void)
{
	long page_size;

	setup_console();
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);

	page_size = sysconf(_SC_PAGESIZE);
	if (page_size <= 0) {
		fprintf(stderr, "VMA_UNMAP invalid page size\n");
		report_result(0);
		power_off();
	}

	if (test_unmap_ranges(page_size)) {
		report_result(0);
		power_off();
	}

	report_result(1);
	power_off();
	return 0;
}
