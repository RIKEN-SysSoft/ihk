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

const char param[] = "interrupt enabled / disabled";
const char *values[] = {
	"enabled",
	"disabled"
};

int main(int argc, char **argv)
{
	int ret;
	int i;
	pid_t pid = -1;

	params_getopt(argc, argv);

	int ret_expected[] = {
		0,
		0,
	};

	enum ihklib_os_status status_expected[] = {
		IHK_STATUS_INACTIVE,
		IHK_STATUS_INACTIVE,
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

		if (i == 1) {
			ret = user_fork_exec("delay_with_interrupt_disabled",
					     &pid);
			INTERR(ret < 0, "user_fork_exec returned %d\n", ret);
		}

		/* wait until McKernel waits with interrupt disabled */
		usleep(1000000);

		INFO("trying to shutdown os\n");
		ret = ihk_os_shutdown(0);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		/* check if os status changed to the expected one */
		os_wait_for_status(status_expected[i]);
		ret = ihk_os_get_status(0);
		OKNG(ret == status_expected[i],
		     "status: %d, expected: %d\n",
		     ret, status_expected[i]);

		/* Clean up */
		if (i == 1) {
			user_wait(&pid);
			linux_kill_mcexec();
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
	if (pid > 0) {
		user_wait(&pid);
		linux_kill_mcexec();
	}

	if (ihk_get_num_os_instances(0)) {
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

