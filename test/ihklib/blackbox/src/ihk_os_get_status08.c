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

	enum ihklib_os_status shutdown_ready_status[] = {
		IHK_STATUS_INACTIVE,
		IHK_STATUS_RUNNING,
		IHK_STATUS_RUNNING,
		IHK_STATUS_INACTIVE,
		IHK_STATUS_PANIC,
		IHK_STATUS_HUNGUP,
		IHK_STATUS_FROZEN,
		IHK_STATUS_FROZEN,
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
			INTERR(fd < 0, "ihklib_os_open returned %d\n",
			       fd);

			ioctl(fd, IHK_OS_DETECT_HUNGUP);
			usleep(0.25 * 1000000);
			ioctl(fd, IHK_OS_DETECT_HUNGUP);

			close(fd);
			break;
		case IHK_STATUS_FREEZING:
		case IHK_STATUS_FROZEN:
			INFO("trying to freeze...\n");
			ret = ihk_os_freeze(os_set, sizeof(unsigned long) * 8);
			INTERR(ret, "ihk_os_freeze returned %d\n", ret);
			break;
		default:
			break;
		}

		/* wait until os status changes to the target one */
		os_wait_for_status(target_status[i]);
		ret = ihk_os_get_status(0);
		OKNG(ret == target_status[i],
		     "status: %d, expected: %d\n",
		     ret, target_status[i]);

		/* wait until os status changes to shutdown-ready one */
		ret = os_wait_for_status(shutdown_ready_status[i]);
		INTERR(ret, "os status didn't change to %d\n",
		       shutdown_ready_status[i]);

		INFO("trying to shutdown os\n");
		ret = ihk_os_shutdown(0);
		INTERR(ret, "ihk_os_shutdown returned %d\n", ret);

		/* wait until os status stablizes */
		ret = os_wait_for_status(IHK_STATUS_INACTIVE);
		INTERR(ret, "os status didn't change to %d\n",
		       IHK_STATUS_INACTIVE);

		/* Clean up */
		switch (target_status[i]) {
		case IHK_STATUS_SHUTDOWN:
		case IHK_STATUS_HUNGUP:
		case IHK_STATUS_PANIC:
			ret = user_wait(&pid);
			INTERR(ret, "user_wait returned %d\n", ret);
			break;
		default:
			break;
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
	if (pid != -1) {
		user_wait(&pid);
		linux_kill_mcexec();
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
		exit(1);
		ihk_os_shutdown(0);
		os_wait_for_status(IHK_STATUS_INACTIVE);
		ihk_destroy_os(0, 0);
	}
	cpus_release();
	mems_release();
	linux_rmmod(1);

	return ret;
}
