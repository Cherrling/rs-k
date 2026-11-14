// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define PRIVATE_ROUNDS 32

static atomic_int shared_value;

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
		"copy_mm userspace: PASS\n" :
		"copy_mm userspace: FAIL\n";
	int kmsg = open("/dev/kmsg", O_WRONLY);

	if (kmsg >= 0) {
		write(kmsg, result, strlen(result));
		close(kmsg);
	}
	write(STDOUT_FILENO, result, strlen(result));
}

static void *copy_mm_thread(void *arg)
{
	atomic_store_explicit(&shared_value, (int)(long)arg, memory_order_release);
	return NULL;
}

static int test_private_address_space(void)
{
	int round;

	for (round = 0; round < PRIVATE_ROUNDS; round++) {
		int child_value = round;
		int status;
		pid_t pid;

		pid = fork();
		if (pid < 0) {
			fprintf(stderr, "COPY_MM fork failed: %s\n", strerror(errno));
			return 1;
		}
		if (pid == 0) {
			child_value = -1;
			_exit(child_value == -1 ? 0 : 1);
		}

		if (waitpid(pid, &status, 0) != pid) {
			fprintf(stderr, "COPY_MM waitpid failed: %s\n",
				strerror(errno));
			return 1;
		}
		if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
			fprintf(stderr, "COPY_MM child failed in round %d\n", round);
			return 1;
		}
		if (child_value != round) {
			fprintf(stderr, "COPY_MM child modified parent state\n");
			return 1;
		}
	}

	return 0;
}

static int test_shared_address_space(void)
{
	int round;

	for (round = 0; round < PRIVATE_ROUNDS; round++) {
		pthread_t thread;
		int expected = round + 1;
		int error;

		atomic_store_explicit(&shared_value, 0, memory_order_relaxed);
		error = pthread_create(&thread, NULL, copy_mm_thread,
				       (void *)(long)expected);
		if (error) {
			fprintf(stderr, "COPY_MM pthread_create failed: %s\n",
				strerror(error));
			return 1;
		}
		error = pthread_join(thread, NULL);
		if (error) {
			fprintf(stderr, "COPY_MM pthread_join failed: %s\n",
				strerror(error));
			return 1;
		}
		if (atomic_load_explicit(&shared_value, memory_order_acquire) !=
		    expected) {
			fprintf(stderr, "COPY_MM CLONE_VM state was not shared\n");
			return 1;
		}
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
	setup_console();
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);

	if (test_private_address_space()) {
		report_result(0);
		power_off();
	}
	if (test_shared_address_space()) {
		report_result(0);
		power_off();
	}

	report_result(1);
	power_off();
	return 0;
}
