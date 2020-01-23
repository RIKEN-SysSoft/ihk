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

const char param[] = "LWK status";
const char *messages[] = {
	"IHK_STATUS_INACTIVE"
	" (before smp_ihk_os_boot or after smp_ihk_destroy_os)",
	"IHK_STATUS_RUNNING (after done_init)",
	"IHK_STATUS_SHUTDOWN (smp_ihk_os_shutdown -- smp_ihk_destroy_os)",
	"IHK_STATUS_PANIC",
	"IHK_STATUS_HUNGUP",
	"IHK_STATUS_FREEZING",
	"IHK_STATUS_FROZEN"
};

#define MAX_COUNT 10

int main(int argc, char **argv)
{
	int ret;
	int i;
	pid_t pid_status = -1;
	pid_t pid_count = -1;
	int fd_fifo = -1;
	int fd = -1;
	char *fn = NULL;

	params_getopt(argc, argv);

	/* Parse additional options */
	int opt;

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

	enum ihklib_os_status target_status[] = {
		IHK_STATUS_INACTIVE,
		IHK_STATUS_RUNNING,
		IHK_STATUS_SHUTDOWN, /* shutting-down */
		IHK_STATUS_PANIC,
		IHK_STATUS_HUNGUP,
		IHK_STATUS_FREEZING,
		IHK_STATUS_FROZEN,
	};

	int ret_expected[] = {
		-EINVAL,
		-EINVAL,
		-EINVAL,
		-EINVAL,
		-EINVAL,
		0,
		0,
	};

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	ret = cpus_reserve();
	INTERR(ret, "cpus_reserve returned %d\n", ret);

	ret = mems_reserve();
	INTERR(ret, "mems_reserve returned %d\n", ret);

	unsigned long os_set[1] = { 1 };

	/* Activate and check */
	for (i = 0; i < 7; i++) {
		int word = 1;
		int count = 0;
		int wstatus;
		char exit_status;
		char cmd[4096];

		START("test-case: %s: %s\n", param, messages[i]);

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

		switch (target_status[i]) {
		case IHK_STATUS_INACTIVE:
			break;
		case IHK_STATUS_RUNNING:
		case IHK_STATUS_SHUTDOWN:
		case IHK_STATUS_PANIC:
		case IHK_STATUS_HUNGUP:
		case IHK_STATUS_FREEZING:
		case IHK_STATUS_FROZEN:
			ret = ihk_os_boot(0);
			INTERR(ret, "ihk_os_boot returned %d\n", ret);
			break;
		default:
			break;
		}

		switch (target_status[i]) {
		case IHK_STATUS_SHUTDOWN:
			pid_status = fork();
			if (!pid_status) {
				ret = ihk_os_shutdown(0);
				if (ret) {
					printf("child: ihk_os_shutdown "
					       "returned %d\n", ret);
				}
				exit(ret);
			}
			break;
		case IHK_STATUS_PANIC:
			ret = user_fork_exec("panic", &pid_status);
			INTERR(ret < 0, "user_fork_exec returned %d\n", ret);
			break;
		case IHK_STATUS_HUNGUP:
			ret = user_fork_exec("hungup", &pid_status);
			INTERR(ret < 0, "user_fork_exec returned %d\n", ret);

			/* wait until McKernel start ihk_mc_delay_us() */
			usleep(0.25 * 1000000);

			fd = ihklib_os_open(0);
			INTERR(fd < 0, "ihklib_os_open returned %d\n", fd);

			ioctl(fd, IHK_OS_DETECT_HUNGUP);
			usleep(0.25 * 1000000);
			ioctl(fd, IHK_OS_DETECT_HUNGUP);

			close(fd);
			break;
		default:
			break;
		}

		if (ret_expected[i] == 0) {
			ret = os_wait_for_status(IHK_STATUS_RUNNING);
			INTERR(ret, "os status didn't change to %d\n",
			       IHK_STATUS_RUNNING);

			fd_fifo = open(fn, O_RDWR);
			INTERR(fd_fifo == -1, "open returned %d\n", errno);

			sprintf(cmd, "count %s", fn);
			ret = user_fork_exec(cmd, &pid_count);
			INTERR(ret < 0, "user_fork_exec returned %d\n", ret);

			/* Start counting */
			ret = write(fd_fifo, &word, sizeof(int));
			INTERR(ret != sizeof(int),
			       "write returned %d\n", errno);

			/* Wait until few messages sent from child */
			usleep(2000000);
		}

		switch (target_status[i]) {
		case IHK_STATUS_FREEZING:
		case IHK_STATUS_FROZEN:
			INFO("trying to freeze...\n");
			ihk_os_freeze(os_set, sizeof(unsigned long) * 8);
			break;
		default:
			break;
		}

		/* wait until os status changes to the target status */
		ret = os_wait_for_status(target_status[i]);
		INTERR(ret, "os status didn't change to %d\n",
		       target_status[i]);

		if (ret_expected[i] == 0) {
			/* Consume messages sent before getting frozen */
			while ((ret = user_poll_fifo(fd_fifo, MAX_COUNT)) > 0) {
				count += ret;
				INFO("# of messages received: %d\n", count);
			}

			/* epoll on pipe should eventually time out */
			OKNG(ret == -ETIME && count < MAX_COUNT,
			     "process becomes silent as expected, ret: %d, count: %d\n",
			     ret, count);
		}

		INFO("trying to thaw...\n");
		ret = ihk_os_thaw(os_set, sizeof(unsigned long) * 8);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		if (ret_expected[i] == 0) {
			/* Consume remaining messages */
			while ((ret = user_poll_fifo(fd_fifo, MAX_COUNT)) > 0) {
				count += ret;
				INFO("# of messages received: %d\n", count);
			}
			OKNG(ret == -ETIME && count == MAX_COUNT,
			     "all messages are received\n");

			ret = waitpid(pid_count, &wstatus, 0);
			exit_status = WEXITSTATUS(wstatus);
			INTERR(ret < 0 || exit_status != 0,
			       "waitpid returned %d, exit status: %d\n",
			       errno, exit_status);
			pid_count = -1;

			close(fd_fifo);
		}

		/* Clean up */

		/* Wait until status stabilizes */
		switch (target_status[i]) {
		case IHK_STATUS_SHUTDOWN:
			ret = os_wait_for_status(IHK_STATUS_INACTIVE);
			INTERR(ret, "os_wait_for_status returned %d\n", ret);
			break;
		default:
			break;
		}

		/* Thaw when frozen */
		if (ihk_os_get_status(0) == IHK_STATUS_FROZEN) {
			ret = ihk_os_thaw(os_set, sizeof(unsigned long) * 8);
			INTERR(ret, "ihk_os_thaw returned %d\n", ret);
		}

		switch (target_status[i]) {
		case IHK_STATUS_SHUTDOWN:
		case IHK_STATUS_HUNGUP:
		case IHK_STATUS_PANIC:
			ret = user_wait(&pid_status);
			INTERR(ret, "user_wait returned %d\n", ret);
			break;
		default:
			break;
		}

		linux_kill_mcexec();

		if (ihk_os_get_status(0) != IHK_STATUS_INACTIVE) {
			ret = ihk_os_shutdown(0);
			INTERR(ret, "ihk_os_shutdown returned %d\n", ret);

			ret = os_wait_for_status(IHK_STATUS_INACTIVE);
			INTERR(ret, "os_wait_for_status returned %d\n", ret);
		}

		ret = cpus_os_release();
		INTERR(ret, "cpus_os_release returned %d\n", ret);

		ret = mems_os_release();
		INTERR(ret, "mems_os_release returned %d\n", ret);

		ret = ihk_destroy_os(0, 0);
		INTERR(ret, "ihk_destroy_os returned %d\n", ret);
	}

	ret = 0;
 out:
	if (pid_status > 0) {
		user_wait(&pid_status);
		linux_kill_mcexec();
	}

	if (ihk_get_num_os_instances(0)) {
		unsigned long os_set[1] = { 1 };

		switch (ihk_os_get_status(0)) {
		case IHK_STATUS_SHUTDOWN:
			os_wait_for_status(IHK_STATUS_INACTIVE);
			break;
		case IHK_STATUS_FREEZING:
			os_wait_for_status(IHK_STATUS_FROZEN);
			break;
		default:
			break;
		}

		if (ihk_os_get_status(0) == IHK_STATUS_FROZEN) {
			ihk_os_thaw(os_set, sizeof(unsigned long) * 8);
			os_wait_for_status(IHK_STATUS_RUNNING);
		}

		if (pid_count > 0) {
			user_wait(&pid_count);
		}

		if (pid_status > 0) {
			user_wait(&pid_status);
		}

		linux_kill_mcexec();

		if (ihk_os_get_status(0) != IHK_STATUS_INACTIVE) {
			ihk_os_shutdown(0);
			os_wait_for_status(IHK_STATUS_INACTIVE);
		}

		ihk_destroy_os(0, 0);
	}
	cpus_release();
	mems_release();
	linux_rmmod(0);

	return ret;
}

