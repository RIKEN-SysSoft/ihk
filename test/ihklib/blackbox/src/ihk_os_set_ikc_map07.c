#include <stdio.h>
#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "mem.h"
#include "cpu.h"
#include "os.h"
#include "params.h"
#include "linux.h"

const char param[] = "map";
const char *values[] = {
	"(1st half of LWK cpus) --> 1st Linux CPU, (2nd half of LWK cpus) --> 2nd Linux CPU",
};

int main(int argc, char **argv)
{
	int ret;
	int i;
	FILE *fp = NULL;

	params_getopt(argc, argv);

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	ret = cpus_reserve();
	INTERR(ret, "cpus_reserve returned %d\n", ret);

	ret = mems_reserve();
	INTERR(ret, "mems_reserve returned %d\n", ret);

	struct ikc_cpu_map map_input[1] = { 0 };

	for (i = 0; i < 1; i++) {
		ret = ikc_cpu_map_2toN(&map_input[i]);
		INTERR(ret, "ikc_cpu_map_2toN returned %d\n", ret);
	}

	int ret_expected[1] = { 0 };

	struct ikc_cpu_map *map_expected[1] = {
		  &map_input[0],
	};

	/* Activate and check */
	for (i = 0; i < 1; i++) {

		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_create_os(0);
		INTERR(ret, "ihk_create_os returned %d\n", ret);

		ret = cpus_os_assign();
		INTERR(ret, "cpus_os_assign returned %d\n", ret);

		ret = mems_os_assign();
		INTERR(ret, "mems_os_assign returned %d\n", ret);

		ret = ihk_os_set_ikc_map(0, map_input[i].map,
					 map_input[i].ncpus);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		ret = os_load();
		INTERR(ret, "os_load returned %d\n", ret);

		ret = os_kargs();
		INTERR(ret, "os_kargs returned %d\n", ret);

		ret = ihk_os_boot(0);
		INTERR(ret, "ihk_os_boot returned %d\n", ret);

		ret = ikc_cpu_map_check_channels(map_expected[i]->ncpus);
		OKNG(ret == 0, "all IKC channels are active\n");

		ret = linux_kill_mcexec();
		INTERR(ret, "linux_kill_mcexec returned %d\n", ret);

		if (map_expected[i]) {
			ret = ikc_cpu_map_check(map_expected[i]);
			OKNG(ret == 0, "ikc map configured as expected\n");
		}

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
	if (fp) {
		fclose(fp);
	}
	linux_kill_mcexec();
	if (ihk_get_num_os_instances(0)) {
		ihk_os_shutdown(0);
		os_wait_for_status(IHK_STATUS_INACTIVE);
		cpus_os_release();
		mems_os_release();
		ihk_destroy_os(0, 0);
	}
	mems_release();
	cpus_release();
	linux_rmmod(0);

	return ret;
}

