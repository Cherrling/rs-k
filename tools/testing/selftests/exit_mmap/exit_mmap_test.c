// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
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

#define EXIT_ROUNDS	64
#define EXIT_DATA_VMAS	32
#define EXIT_DATA_PAGES	4
#define EXIT_FILE_PAGES	8

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
		"exit_mmap userspace: PASS\n" :
		"exit_mmap userspace: FAIL\n";
	int kmsg = open("/dev/kmsg", O_WRONLY);

	if (kmsg >= 0) {
		write(kmsg, result, strlen(result));
		close(kmsg);
	}
	write(STDOUT_FILENO, result, strlen(result));
}

static int child_exit_mappings(size_t page_size)
{
	const size_t data_size = EXIT_DATA_PAGES * page_size;
	const size_t span = EXIT_DATA_VMAS * data_size * 2;
	unsigned char *span_address;
	void *shared_page;
	void *file_page;
	char *address;
	int file_fd;
	int index;

	span_address = mmap(NULL, span, PROT_NONE,
			    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (span_address == MAP_FAILED)
		return 1;

	for (index = 0; index < EXIT_DATA_VMAS; index++) {
		address = (char *)span_address + index * data_size * 2;
		if (mprotect(address, data_size, PROT_READ | PROT_WRITE))
			return 2;
		memset(address, index + 1, data_size);
		asm volatile("" : : "r"(address) : "memory");
	}

	shared_page = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
			   MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (shared_page == MAP_FAILED)
		return 3;
	memset(shared_page, 0x5a, page_size);

	file_fd = memfd_create("exit-mmap", 0);
	if (file_fd < 0)
		return 4;
	if (ftruncate(file_fd, EXIT_FILE_PAGES * page_size))
		return 5;
	file_page = mmap(NULL, EXIT_FILE_PAGES * page_size,
			 PROT_READ | PROT_WRITE, MAP_SHARED, file_fd, 0);
	if (file_page == MAP_FAILED)
		return 6;
	memset(file_page, 0xa5, EXIT_FILE_PAGES * page_size);

	return 0;
}

static int wait_for_child(pid_t pid, int round)
{
	int status;

	while (waitpid(pid, &status, 0) < 0) {
		if (errno != EINTR) {
			fprintf(stderr,
				"EXIT_MMAP waitpid failed in round %d: %s\n",
				round, strerror(errno));
			return 1;
		}
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status)) {
		fprintf(stderr,
			"EXIT_MMAP child failed in round %d: status %#x\n",
			round, status);
		return 1;
	}
	return 0;
}

static int test_exit_mappings(size_t page_size)
{
	int round;

	for (round = 0; round < EXIT_ROUNDS; round++) {
		pid_t pid = fork();

		if (pid < 0) {
			fprintf(stderr,
				"EXIT_MMAP fork failed in round %d: %s\n",
				round, strerror(errno));
			return 1;
		}
		if (!pid) {
			int error = child_exit_mappings(page_size);

			_exit(error);
		}
		if (wait_for_child(pid, round))
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
		fprintf(stderr, "EXIT_MMAP invalid page size\n");
		report_result(0);
		power_off();
	}

	if (test_exit_mappings(page_size)) {
		report_result(0);
		power_off();
	}

	report_result(1);
	power_off();
	return 0;
}
