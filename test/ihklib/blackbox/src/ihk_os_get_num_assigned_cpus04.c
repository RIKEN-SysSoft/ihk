#include <limits.h>
#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "cpu.h"
#include "os.h"
#include "mem.h"
#include "params.h"
#include "linux.h"

const char param[] = "os status";
const char *values[] = {
	"before boot",
	"after boot",
};

int main(int argc, char **argv)
{
	int ret;
	int i;

	params_getopt(argc, argv);

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "insmod returned %d\n", ret);

	ret = cpus_reserve();
	INTERR(ret, "cpus_reserve returned %d\n", ret);

	ret = mems_reserve();
	INTERR(ret, "mems_reserve returned %d\n", ret);

	ret = ihk_create_os(0);
	INTERR(ret, "ihk_create_os returned %d\n", ret);

	struct cpus cpus_expected[2] = { 0 };

	/* All of McKernel CPUs */
	for (i = 0; i < 2; i++) {
		ret = cpus_reserved(&cpus_expected[i]);
		INTERR(ret, "cpus_reserved returned %d\n", ret);
	}

	int ret_expected[2] = {
		cpus_expected[0].ncpus,
		cpus_expected[1].ncpus,
	};

	ret = cpus_os_assign();
	INTERR(ret, "cpus_os_assign returned %d\n", ret);

	ret = mems_os_assign();
	INTERR(ret, "mems_os_assign returned %d\n", ret);

	/* Activate and check */
	for (i = 0; i < 2; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		if (i == 1) {
			ret = os_load();
			INTERR(ret, "os_load returned %d\n", ret);

			ret = os_kargs();
			INTERR(ret, "os_kargs returned %d\n", ret);

			ret = ihk_os_boot(0);
			INTERR(ret, "ihk_os_boot returned %d\n", ret);
		}

		ret = ihk_os_get_num_assigned_cpus(0);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		if (i == 1) {
			ret = ihk_os_shutdown(0);
			INTERR(ret, "ihk_os_shutdown returned %d\n", ret);

			ret = os_wait_for_status(IHK_STATUS_INACTIVE);
			INTERR(ret, "os status didn't change to %d\n",
			       IHK_STATUS_INACTIVE);
		}

	}

	ret = 0;
 out:
	cpus_os_release();
	mems_os_release();
	ihk_destroy_os(0, 0);
	cpus_release();
	mems_release();
	linux_rmmod(0);

	return ret;
}
