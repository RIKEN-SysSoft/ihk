#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <ihklib.h>
#include <ihk/ihklib_private.h>
#include "util.h"
#include "okng.h"
#include "cpu.h"
#include "mem.h"
#include "os.h"
#include "user.h"
#include "params.h"
#include "linux.h"

#define MAX_COUNT 10

const char param[] = "os bitmap";
const char *values[] = {
	"1st bit is set",
	"2nd bit is set",
};

int main(int argc, char **argv)
{
	int ret;
	int i;
	char *fn = NULL;
	int fd_fifo;
	int opt;
	unsigned long os_set[2][1] = {
		{ 1 },
		{ 2 },
	};
	int pid = -1;

	params_getopt(argc, argv);

	while ((opt = getopt(argc, argv, "f:")) != -1) {
		switch (opt) {
		case 'f':
			fn = optarg;
			break;
		default: /* '?' */
			printf("unknown option %c\n", optopt);
			exit(1);
		}
	}

	int ret_expected[] = {
		0,
		-ENOENT,
	};

	int ret_expected_wait_status[2] = {
		0,
		-ETIMEDOUT
	};

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	ret = cpus_reserve();
	INTERR(ret, "cpus_reserve returned %d\n", ret);

	ret = mems_reserve();
	INTERR(ret, "mems_reserve returned %d\n", ret);

	/* Activate and check */
	for (i = 0; i < 2; i++) {
		char cmd[4096];
		int word = 1;
		int wstatus;
		int count = 0;

		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_create_os(0);
		INTERR(ret, "ihk_create_os returned %d\n", ret);

		ret = cpus_os_assign();
		INTERR(ret, "cpus_os_assign returned %d\n", ret);

		ret = mems_os_assign();
		INTERR(ret, "mems_os_assign returned %d\n", ret);

		ret = os_load();
		INTERR(ret, "os_load returned %d\n", ret);

		ret = os_kargs();
		INTERR(ret, "os_kargs returned %d\n", ret);

		ret = ihk_os_boot(0);
		INTERR(ret, "ihk_os_boot returned %d\n", ret);

		ret = os_wait_for_status(IHK_STATUS_RUNNING);
		INTERR(ret, "os_wait_for_status timeout %d\n", ret);

		sprintf(cmd, "count %s", fn);
		ret = user_fork_exec(cmd, &pid);
		INTERR(ret < 0, "user_fork_exec returned %d\n", ret);

		fd_fifo = open(fn, O_RDWR);
		INTERR(fd_fifo == -1, "open returned %d\n", errno);

		/* Start counting */
		ret = write(fd_fifo, &word, sizeof(int));
		INTERR(ret != sizeof(int),
		       "write returned %d\n", errno);

		/* Wait until few messages sent from child */
		usleep(2000000);

		INFO("trying to freeze os\n");
		ret = ihk_os_freeze(os_set[i], 8 * sizeof(unsigned long));
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		ret = os_wait_for_status(IHK_STATUS_FROZEN);
		OKNG(ret == ret_expected_wait_status[i],
		     "os status %s to FROZEN\n",
		     ret == 0 ? "has changed" : "didn't change");

		while ((ret = user_poll_fifo(fd_fifo, MAX_COUNT)) > 0) {
			count += ret;
			INFO("# of messages received: %d\n", count);
		}

		if (ihk_os_get_status(0) == IHK_STATUS_FROZEN) {
			/* epoll on pipe should eventually time out */
			OKNG(ret == -ETIME && count < MAX_COUNT,
			     "process becomes silent as expected, ret: %d\n",
			     ret);

			INFO("trying to thaw...\n");
			ret = ihk_os_thaw(os_set[i], sizeof(unsigned long) * 8);
			INTERR(ret, "ihk_os_thaw returned %d\n", ret);

			/* Consume remaining messages */
			while ((ret = user_poll_fifo(fd_fifo, MAX_COUNT)) > 0) {
				count += ret;
				INFO("# of messages received: %d\n", count);
			}
		}

		OKNG(ret == -ETIME && count == MAX_COUNT,
		     "all messages are received\n");

		ret = waitpid(pid, &wstatus, 0);
		INTERR(ret < 0 || WEXITSTATUS(wstatus) != 0,
		       "waitpid returned %d, exit status: %d\n",
		       errno, WEXITSTATUS(wstatus));
		pid = -1;

		close(fd_fifo);

		/* Clean up */
		ret = ihk_os_shutdown(0);
		INTERR(ret, "ihk_os_shutdown returned %d\n", ret);

		ret = os_wait_for_status(IHK_STATUS_INACTIVE);
		INTERR(ret, "os status didn't change to %d\n",
		       IHK_STATUS_INACTIVE);

		ret = cpus_os_release();
		INTERR(ret, "cpus_os_release returned %d\n", ret);

		ret = mems_os_release();
		INTERR(ret, "mems_os_release returned %d\n", ret);

		ret = ihk_destroy_os(0, 0);
		INTERR(ret, "ihk_destroy_os returned %d\n", ret);
	}

	ret = 0;
 out:
	if (pid != -1) {
		user_wait(&pid);
		linux_kill_mcexec();
	}
	if (ihk_get_num_os_instances(0)) {
		if (ihk_os_get_status(0) == IHK_STATUS_FROZEN) {
			unsigned long os_set[1] = { 1 };

			ihk_os_thaw(os_set, sizeof(unsigned long) * 8);
			os_wait_for_status(IHK_STATUS_RUNNING);
		}
		ihk_os_shutdown(0);
		os_wait_for_status(IHK_STATUS_INACTIVE);
		cpus_os_release();
		mems_os_release();
		ihk_destroy_os(0, 0);
	}
	cpus_release();
	mems_release();
	linux_rmmod(1);

	return ret;
}
