#include <stdlib.h>
#include <errno.h>
#include <sys/ioctl.h>
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
	"IHK_STATUS_BOOTING"
	" (smp_ihk_os_boot -- arch_init -- arch_ready -- done_init)",
	"IHK_STATUS_RUNNING (after done_init)",
	"IHK_STATUS_SHUTDOWN (smp_ihk_os_shutdown -- smp_ihk_destroy_os)",
	"IHK_STATUS_PANIC",
	"IHK_STATUS_HUNGUP",
	"IHK_STATUS_FREEZING",
	"IHK_STATUS_FROZEN"
};

int main(int argc, char **argv)
{
	int ret;
	int i;
	pid_t pid = -1;

	params_getopt(argc, argv);

	enum ihklib_os_status target_status[] = {
		IHK_STATUS_INACTIVE,
		IHK_STATUS_BOOTING,
		IHK_STATUS_RUNNING,
		IHK_STATUS_SHUTDOWN, /* shutting-down */
		IHK_STATUS_PANIC,
		IHK_STATUS_HUNGUP,
		IHK_STATUS_FREEZING,
		IHK_STATUS_FROZEN,
	};

	int ret_expected[] = {
		0,
		0,
		0,
		-EBUSY,
		0,
		0,
		0,
		0,
	};

	int num_os_instances_after_destroy[] = {
		0,
		0,
		0,
		1,
		0,
		0,
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

	int fd;
	unsigned long os_set[1] = { 1 };

	/* Activate and check */
	for (i = 0; i < 8; i++) {
		/* There's no way to destroy os in BOOTING state
		 * because ihk_os_boot() returns after os status
		 * changes to RUNNING
		 */
		if (i == 1) {
			continue;
		}

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
		case IHK_STATUS_BOOTING:
			/* Manually boot because ihk_os_boot wait for RUNNING */
			fd = ihklib_os_open(0);
			INTERR(fd < 0, "ihklib_os_open returned %d\n",
			       fd);

			ret = ioctl(fd, IHK_OS_BOOT, 0);
			INTERR(ret == -1, "ihk_os_boot: errno: %d\n", errno);

			close(fd);
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
			pid = fork();
			if (!pid) {
				ret = ihk_os_shutdown(0);
				if (ret) {
					printf("child: ihk_os_shutdown "
					       "returned %d\n", ret);
				}
				exit(ret);
			}
			break;
		case IHK_STATUS_PANIC:
			ret = user_fork_exec("panic", &pid);
			INTERR(ret < 0, "user_fork_exec returned %d\n", ret);
			break;
		case IHK_STATUS_HUNGUP:
			ret = user_fork_exec("hungup", &pid);
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
		case IHK_STATUS_FREEZING:
		case IHK_STATUS_FROZEN:
			ihk_os_freeze(os_set, sizeof(unsigned long) * 8);
			break;
		default:
			break;
		}

		/* wait until os status changes to the target status */
		ret = os_wait_for_status(target_status[i]);
		INTERR(ret, "os status didn't change to %d\n",
		       target_status[i]);

		INFO("trying to destroy os\n");
		ret = ihk_destroy_os(0, 0);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		ret = ihk_get_num_os_instances(0);
		OKNG(ret == num_os_instances_after_destroy[i],
		     "os is destroyed as expected\n");

		/* wait until parallel shutdown finishes */
		if (target_status[i] == IHK_STATUS_SHUTDOWN) {
			ret = os_wait_for_status(IHK_STATUS_INACTIVE);
			INTERR(ret, "os status didn't change to %d\n",
			       IHK_STATUS_INACTIVE);

			ret = ihk_destroy_os(0, 0);
			INTERR(ret, "ihk_destroy_os returned %d\n",
			       ret);
		}

		/* Clean up */
		switch (target_status[i]) {
		case IHK_STATUS_HUNGUP:
		case IHK_STATUS_PANIC:
			ret = user_wait(&pid);
			INTERR(ret, "user_wait returned %d\n", ret);
			break;
		default:
			break;
		}

		if (ihk_get_num_os_instances(0)) {
			ret = ihk_destroy_os(0, 0);
			INTERR(ret, "ihk_destroy_os returned %d\n", ret);
		}
	}

	ret = 0;
 out:
	if (pid > 0) {
		user_wait(&pid);
	}

	if (ihk_get_num_os_instances(0)) {
		unsigned long os_set[1] = { 1 };

		switch (ihk_os_get_status(0)) {
		case IHK_STATUS_FREEZING:
			os_wait_for_status(IHK_STATUS_FROZEN);
			/* fall through */
		case IHK_STATUS_FROZEN:
			ihk_os_thaw(os_set, sizeof(unsigned long) * 8);
			break;
		default:
			break;
		}

		ihk_os_shutdown(0);
		os_wait_for_status(IHK_STATUS_INACTIVE);
		ihk_destroy_os(0, 0);
	}
	cpus_release();
	mems_release();
	linux_rmmod(1);

	return ret;
}

