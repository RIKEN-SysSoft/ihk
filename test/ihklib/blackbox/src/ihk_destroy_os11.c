#include <stdlib.h>
#include <errno.h>
#include <ihklib.h>
#include <ihk/ihklib_private.h>
#include "util.h"
#include "okng.h"
#include "cpu.h"
#include "mem.h"
#include "os.h"
#include "params.h"
#include "linux.h"

const char param[] = "/dev/mcos0 open timing";
const char *values[] = {
	 "conflict with ihkmond",
};

int main(int argc, char **argv)
{
	int ret;
	int i;

	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	ret = _cpus_reserve(2, 2);
	INTERR(ret, "cpus_reserve returned %d\n", ret);

	ret = _mems_reserve(4, 0.9, 512UL << 20);
	INTERR(ret, "mems_reserve returned %d\n", ret);

	int ret_expected[] = { 0 };

	int num_os_instances_after_destroy[] = { 0 };

	START("test-case: %s: %s\n", param, values[0]);

	/* chance to conflict is 1% (10 usec / 1 msec),
	 * so probability of missing all * chances is roughly
	 * 2^ -(number_of_iterations / 72)
	 */
	for (i = 0; i < 72 * 20; i++) {
		ret = ihk_create_os(0);
		INTERR(ret, "ihk_create_os returned %d\n", ret);

		//INFO("os created\n");

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

		//INFO("os booted\n");

		ret = ihk_destroy_os(0, 0);
		INTERR(ret != ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		if (i % 10 == 0) {
			INFO("destroy trial #%d succeeded\n", i);
		}

		ret = ihk_get_num_os_instances(0);
		INTERR(ret != num_os_instances_after_destroy[i],
		     "# of os is not zero\n");

		ret = system("dmesg | "
			     "grep -q '__ihk_device_destroy_os: error: refcount != 0 (1)'");
		INTERR(ret < 0, "system failed with %d\n", errno);
		if (WEXITSTATUS(ret) == 0) {
			break;
		}
	}
	OKNG(i < 1000, "successfully dealt with conflict with ihkmond\n");

	ret = 0;
 out:
	if (ihk_get_num_os_instances(0) > 0) {
		ihk_os_shutdown(0);
		os_wait_for_status(IHK_STATUS_INACTIVE);
		cpus_os_release();
		mems_os_release();
		ihk_destroy_os(0, 0);
	}
	cpus_release();
	mems_release();
	linux_rmmod(0);

	return ret;
}

