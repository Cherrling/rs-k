// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define FORK_ROUNDS 32

#define PRIVATE_ORIGINAL	0x31
#define PRIVATE_PARENT		0x41
#define PRIVATE_CHILD		0x52
#define DONT_FORK_VALUE		0x63
#define WIPE_ORIGINAL		0x74
#define WIPE_CHILD		0x85
#define SHARED_CHILD		0x96
#define FILE_ORIGINAL		0xa7
#define FILE_CHILD		0xb8

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
		"dup_mmap userspace: PASS\n" :
		"dup_mmap userspace: FAIL\n";
	int kmsg = open("/dev/kmsg", O_WRONLY);

	if (kmsg >= 0) {
		write(kmsg, result, strlen(result));
		close(kmsg);
	}
	write(STDOUT_FILENO, result, strlen(result));
}

static void fill_page(unsigned char *page, unsigned char value, size_t size)
{
	size_t i;

	for (i = 0; i < size; i++)
		page[i] = value;
}

static int page_is_zero(const unsigned char *page, size_t size)
{
	size_t i;

	for (i = 0; i < size; i++) {
		if (page[i])
			return 0;
	}
	return 1;
}

static int mapping_status(void *address, size_t size)
{
	unsigned char vec;
	int attempt;

	for (attempt = 0; attempt < 8; attempt++) {
		if (!mincore(address, size, &vec))
			return 1;
		if (errno == ENOMEM)
			return 0;
		if (errno != EAGAIN)
			return -1;
		usleep(1000);
	}
	return -1;
}

static void wait_for_child(pid_t pid, int round)
{
	int status;

	while (waitpid(pid, &status, 0) < 0) {
		if (errno != EINTR) {
			fprintf(stderr, "DUP_MMAP waitpid failed in round %d: %s\n",
				round, strerror(errno));
			exit(1);
		}
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status)) {
		fprintf(stderr, "DUP_MMAP child failed in round %d: status %#x\n",
			round, status);
		exit(1);
	}
}

static void child_checks(unsigned char *private_page,
			 unsigned char *dont_fork_page,
			 unsigned char *wipe_page,
			 unsigned char *shared_page,
			 unsigned char *file_page,
			 size_t page_size, int pipe_fd)
{
	char token;
	int status;

	if (read(pipe_fd, &token, 1) != 1) {
		fprintf(stderr, "DUP_MMAP child pipe read failed: %s\n",
			strerror(errno));
		_exit(10);
	}

	if (private_page[0] != PRIVATE_ORIGINAL) {
		fprintf(stderr, "DUP_MMAP child private value mismatch\n");
		_exit(11);
	}
	private_page[0] = PRIVATE_CHILD;

	status = mapping_status((void *)dont_fork_page, page_size);
	if (status != 0) {
		fprintf(stderr,
			"DUP_MMAP DONTFORK mapping status: %d errno %d\n",
			status, errno);
		_exit(12);
	}

	status = mapping_status((void *)wipe_page, page_size);
	if (status != 1) {
		fprintf(stderr,
			"DUP_MMAP WIPEONFORK mapping status: %d errno %d\n",
			status, errno);
		_exit(13);
	}
	if (!page_is_zero(wipe_page, page_size)) {
		fprintf(stderr, "DUP_MMAP WIPEONFORK page was not zero\n");
		_exit(14);
	}
	wipe_page[0] = WIPE_CHILD;

	shared_page[0] = SHARED_CHILD;
	if (file_page[0] != FILE_ORIGINAL)
		_exit(15);
	file_page[0] = FILE_CHILD;
	_exit(0);
}

static int test_fork_mappings(size_t page_size)
{
	int round;

	for (round = 0; round < FORK_ROUNDS; round++) {
		unsigned char *private_page = MAP_FAILED;
		unsigned char *dont_fork_page = MAP_FAILED;
		unsigned char *wipe_page = MAP_FAILED;
		unsigned char *shared_page = MAP_FAILED;
		unsigned char *file_page = MAP_FAILED;
		int pipe_fd[2];
		int file_fd = -1;
		pid_t pid;
		char token = 1;
		int error = 1;

		file_fd = memfd_create("dup-mmap", 0);
		if (file_fd < 0 || ftruncate(file_fd, page_size)) {
			fprintf(stderr,
				"DUP_MMAP memfd setup failed in round %d: %s\n",
				round, strerror(errno));
			goto out;
		}
		private_page = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
				    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		dont_fork_page = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
				      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		wipe_page = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
				 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		shared_page = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
				   MAP_SHARED | MAP_ANONYMOUS, -1, 0);
		file_page = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
				 MAP_SHARED, file_fd, 0);
		if (private_page == MAP_FAILED || dont_fork_page == MAP_FAILED ||
		    wipe_page == MAP_FAILED || shared_page == MAP_FAILED ||
		    file_page == MAP_FAILED) {
			fprintf(stderr, "DUP_MMAP mmap failed in round %d: %s\n",
				round, strerror(errno));
			goto out;
		}

		fill_page(private_page, PRIVATE_ORIGINAL, page_size);
		fill_page(dont_fork_page, DONT_FORK_VALUE, page_size);
		fill_page(wipe_page, WIPE_ORIGINAL, page_size);
		shared_page[0] = 0;
		file_page[0] = FILE_ORIGINAL;

		if (madvise((void *)dont_fork_page, page_size, MADV_DONTFORK) ||
		    madvise((void *)wipe_page, page_size, MADV_WIPEONFORK)) {
			fprintf(stderr,
				"DUP_MMAP madvise failed in round %d: %s\n",
				round, strerror(errno));
			goto out;
		}
		if (pipe(pipe_fd)) {
			fprintf(stderr, "DUP_MMAP pipe failed in round %d: %s\n",
				round, strerror(errno));
			goto out;
		}

		pid = fork();
		if (pid < 0) {
			fprintf(stderr, "DUP_MMAP fork failed in round %d: %s\n",
				round, strerror(errno));
			close(pipe_fd[0]);
			close(pipe_fd[1]);
			goto out;
		}
		if (!pid) {
			close(pipe_fd[1]);
			child_checks(private_page, dont_fork_page, wipe_page,
				     shared_page, file_page, page_size, pipe_fd[0]);
		}

		close(pipe_fd[0]);
		private_page[0] = PRIVATE_PARENT;
		if (write(pipe_fd[1], &token, 1) != 1) {
			fprintf(stderr, "DUP_MMAP pipe write failed in round %d\n",
				round);
			close(pipe_fd[1]);
			wait_for_child(pid, round);
			goto out;
		}
		close(pipe_fd[1]);
		wait_for_child(pid, round);

		if (private_page[0] != PRIVATE_PARENT) {
			fprintf(stderr,
				"DUP_MMAP private write leaked in round %d\n",
				round);
			goto out;
		}
		if (dont_fork_page[0] != DONT_FORK_VALUE) {
			fprintf(stderr,
				"DUP_MMAP parent mapping changed in round %d\n",
				round);
			goto out;
		}
		if (wipe_page[0] != WIPE_ORIGINAL) {
			fprintf(stderr,
				"DUP_MMAP wiped parent mapping in round %d\n",
				round);
			goto out;
		}
		if (shared_page[0] != SHARED_CHILD) {
			fprintf(stderr,
				"DUP_MMAP shared write missing in round %d\n",
				round);
			goto out;
		}
		if (file_page[0] != FILE_CHILD) {
			fprintf(stderr,
				"DUP_MMAP file write missing in round %d\n",
				round);
			goto out;
		}

		error = 0;
out:
		if (private_page != MAP_FAILED)
			munmap((void *)private_page, page_size);
		if (dont_fork_page != MAP_FAILED)
			munmap((void *)dont_fork_page, page_size);
		if (wipe_page != MAP_FAILED)
			munmap((void *)wipe_page, page_size);
		if (shared_page != MAP_FAILED)
			munmap((void *)shared_page, page_size);
		if (file_page != MAP_FAILED)
			munmap((void *)file_page, page_size);
		if (file_fd >= 0)
			close(file_fd);
		if (error)
			return error;
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
		fprintf(stderr, "DUP_MMAP invalid page size\n");
		report_result(0);
		power_off();
	}
	if (test_fork_mappings(page_size)) {
		report_result(0);
		power_off();
	}

	report_result(1);
	power_off();
	return 0;
}
