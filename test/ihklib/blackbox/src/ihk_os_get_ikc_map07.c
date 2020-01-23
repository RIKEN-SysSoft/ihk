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
			"NULL",
			"2-(N/2):0+(N/2+1)-(N-1):1",
			"2-(N/2):0+(N/2+1)-(N):1",
			"2-(N/2):0+(N/2+1)-(N-2):1",
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

	struct ikc_cpu_map map_input_map[4] = { 0 };

	for (i = 0; i < 4; i++) {
		ret = ikc_cpu_map_2toN(&map_input_map[i]);
		INTERR(ret, "ikc_cpu_map_2toN returned %d\n", ret);
	}

	struct ikc_cpu_map map_input[4] = {
		{ .ncpus = 1, .map = NULL },
		{ 0 },
		{ 0 },
		{ 0 },
	};

	for (i = 1; i < 4; i++) {
		ret = ikc_cpu_map_2toN(&map_input[i]);
		INTERR(ret, "ikc_cpu_map_2toN returned %d\n", ret);
	}

	int src_cpu, dst_cpu;

	ikc_cpu_map_max_src_cpu(&map_input[2], &src_cpu, &dst_cpu);
	ret = ikc_cpu_map_push(&map_input[2], src_cpu + 1, dst_cpu);
	INTERR(ret, "ikc_cpu_map_push returned %d\n", ret);
	//ikc_cpu_map_dump(&map_input[2]);

	ret = ikc_cpu_map_pop(&map_input[3], 1);
	INTERR(ret, "ikc_cpu_map_pop returned %d\n", ret);

	int ret_expected[4] = {
		-EFAULT,
		0,
		-EINVAL,
		-EINVAL,
	};

	struct ikc_cpu_map map_after_map[4] = { 0 };

	for (i = 0; i < 4; i++) {
		ret = ikc_cpu_map_2toN(&map_after_map[i]);
		INTERR(ret, "ikc_cpu_map_2toN returned %d\n", ret);
	}

	struct ikc_cpu_map *map_expected[] = {
		NULL,
		&map_after_map[1],
		NULL,
		NULL,
	};

	/* Activate and check */
	for (i = 0; i < 4; i++) {
		int errno_save;
		int ncpu;
		char cmd[4096];

		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_create_os(0);
		INTERR(ret, "ihk_create_os returned %d\n", ret);

		ret = cpus_os_assign();
		INTERR(ret, "cpus_os_assign returned %d\n", ret);

		ret = mems_os_assign();
		INTERR(ret, "mems_os_assign returned %d\n", ret);

		ret = ihk_os_set_ikc_map(0, map_input_map[i].map,
					 map_input_map[i].ncpus);
		INTERR(ret, "ihk_os_set_ikc_map returned %d\n", ret);

		ret = os_load();
		INTERR(ret, "os_load returned %d\n", ret);

		ret = os_kargs();
		INTERR(ret, "os_kargs returned %d\n", ret);

		ret = ihk_os_boot(0);
		INTERR(ret, "ihk_os_boot returned %d\n", ret);

		ret = ihk_os_get_ikc_map(0, map_input[i].map,
					 map_input[i].ncpus);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		if (map_expected[i]) {
			ret = ikc_cpu_map_compare(&map_input[i],
						  map_expected[i]);
			OKNG(ret == 0, "map got matches map set\n");
		}

		ret = ihk_os_shutdown(0);
		INTERR(ret, "ihk_os_shutdown returned %d\n", ret);

		ret = os_wait_for_status(IHK_STATUS_INACTIVE);
		INTERR(ret, "os status didn't change to %d\n",
		       IHK_STATUS_INACTIVE);

		ret = ihk_destroy_os(0, 0);
		INTERR(ret, "ihk_destroy_os returned %d\n", ret);
	}

	ret = 0;
 out:
	if (fp) {
		fclose(fp);
	}
	linux_rmmod(0);

	return ret;
}

