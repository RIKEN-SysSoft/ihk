#include <limits.h>
#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "cpu.h"
#include "mem.h"
#include "os.h"
#include "params.h"
#include "linux.h"

const char param[] = "num_cpus";
const char *values[] = {
	"INT_MIN",
	"-1",
	"0",
	"1",
	"all",
	"all - 1",
	"all + 1",
	"INT_MAX",
};

int main(int argc, char **argv)
{
	int ret;
	int i;

	params_getopt(argc, argv);

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	ret = cpus_reserve();
	INTERR(ret, "cpus_reserve returned %d\n", ret);

	ret = mems_reserve();
	INTERR(ret, "mems_reserve returned %d\n", ret);


	int ret_expected[] = {
		-EINVAL,
		-EINVAL,
		-EINVAL,
		-EINVAL,
		0,
		-EINVAL,
		-EINVAL,
		-EINVAL,
	};

	struct ikc_cpu_map map_input[8] = { 0 };
	struct ikc_cpu_map map_after_set[8] = { 0 };
	struct ikc_cpu_map *map_expected[] = {
		NULL,
		NULL,
		NULL,
		NULL,
		&map_after_set[4],
		NULL,
		NULL,
		NULL,
	};

	for (i = 0; i < 8; i++) {
		int src, dst;

		ret = ikc_cpu_map_2toN(&map_input[i]);
		INTERR(ret, "ikc_cpu_map_2toN returned %d\n", ret);

		switch (i) {
		case 0:
			map_input[i].ncpus = INT_MIN;
			break;
		case 1:
			map_input[i].ncpus = -1;
			break;
		case 2: /* 0 */
			ret = ikc_cpu_map_pop(&map_input[i],
					map_input[i].ncpus);
			INTERR(ret, "ikc_cpu_map_pop returned %d\n", ret);
			break;
		case 3: /* 1 */
			ret = ikc_cpu_map_pop(&map_input[i],
					map_input[i].ncpus - 1);
			INTERR(ret, "ikc_cpu_map_pop returned %d\n", ret);
			break;
		case 4: /* all */
			ret = ikc_cpu_map_2toN(&map_after_set[i]);
			INTERR(ret, "ikc_cpu_map_2toN returned %d\n", ret);
			break;
		case 5: /* all - 1 */
			ret = ikc_cpu_map_pop(&map_input[i], 1);
			INTERR(ret, "ikc_cpu_map_pop returned %d\n", ret);
			break;
		case 6:
			ikc_cpu_map_max_src_cpu(&map_input[i], &src, &dst);

			ret = ikc_cpu_map_push(&map_input[i], src + 1, dst);
			INTERR(ret, "ikc_cpu_map_push returned %d\n", ret);
			break;
		case 7:
			map_input[i].ncpus = INT_MAX;
			break;
		}
	}

	/* Activate and check */
	for (i = 0; i < 8; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_create_os(0);
		INTERR(ret, "ihk_create_os returned %d\n", ret);

		ret = cpus_os_assign();
		INTERR(ret, "cpus_os_assign returned %d\n", ret);

		ret = mems_os_assign();
		INTERR(ret, "mems_os_assign returned %d\n", ret);

		ret = ihk_os_set_ikc_map(0, map_input[i].map,
				map_input[i].ncpus);
		OKNG(ret == ret_expected[i], "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		ret = os_load();
		INTERR(ret, "os_load returned %d\n", ret);

		ret = os_kargs();
		INTERR(ret, "os_kargs returned %d\n", ret);

		ret = ihk_os_boot(0);
		INTERR(ret, "ihk_os_boot returned %d\n", ret);

		if (map_expected[i]) {
			ret = ikc_cpu_map_check_channels(map_input[i].ncpus);
			OKNG(ret == 0, "all IKC channels are active\n");

			ret = linux_kill_mcexec();
			INTERR(ret, "linux_kill_mcexec returned %d\n", ret);

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
