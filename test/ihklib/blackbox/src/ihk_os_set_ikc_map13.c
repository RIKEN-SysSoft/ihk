#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "cpu.h"
#include "mem.h"
#include "os.h"
#include "params.h"
#include "linux.h"

const char param[] = "IKC destination CPUs";
const char *values[] = {
	"include assigned CPU(s)",
	"include offlined CPU(s)",
	"include INT_MIN",
	"include -1",
	"include INT_MAX",
};

int main(int argc, char **argv)
{
	int ret;
	int i, j;
	int max_id = -1;

	struct cpus cpus_mckernel = { 0 };
	struct cpus cpus_linux = { 0 };
	struct cpus srcs_invalid[5] = { 0 };
	struct cpus dsts_invalid[5] = { 0 };
	struct ikc_cpu_map map_input[5] = { 0 };
	struct ikc_cpu_map map_after_set[5] = { 0 };
	struct cpus cpu_first = { 0 };

	struct ikc_cpu_map *map_expected[5] = {
		&map_after_set[0],
		&map_after_set[1],
		&map_after_set[2],
		&map_after_set[3],
		&map_after_set[4],
	};

	int ret_expected[5] = {
		  -EINVAL,
		  -EINVAL,
		  -EINVAL,
		  -EINVAL,
		  -EINVAL,
	};

	params_getopt(argc, argv);

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	ret = cpus_ls(&cpu_first);
	INTERR(ret, "cpus_ls returned %d\n", ret);

	ret = cpus_ls(&cpus_mckernel);
	INTERR(ret, "cpus_ls returned %d\n", ret);

	max_id = cpus_max_id(&cpus_mckernel);
	ret = cpus_toggle(max_id, "off");
	INTERR(ret, "cpus_toggle returned %d\n", ret);

	/* linux cpus */
	ret = cpus_ls(&cpus_linux);
	INTERR(ret, "cpus_ls returned %d\n", ret);

	ret = cpus_pop(&cpus_linux, cpus_linux.ncpus - 2);
	INTERR(ret, "cpus_pop returned %d\n", ret);

	/* mckernel cpus */
	ret = cpus_shift(&cpus_mckernel, 2);
	INTERR(ret, "cpus_shift returned %d\n", ret);

	ret = cpus_pop(&cpus_mckernel, 1);
	INTERR(ret, "cpus_pop returned %d\n", ret);

	ret = ihk_reserve_cpu(0, cpus_mckernel.cpus, cpus_mckernel.ncpus);
	INTERR(ret, "ihk_reserve_cpu returned %d\n", ret);

	ret = mems_reserve();
	INTERR(ret, "mems_reserve returned %d\n", ret);

	ret = cpus_reserved(&cpus_mckernel);
	INTERR(ret, "cpus_reserved returned %d\n", ret);

	for (i = 0; i < 4; i++) {
		/* create input ikc maps */
		int last_idx;

		ret = cpus_reserved(&srcs_invalid[i]);
		INTERR(ret, "cpus_reserve returned %d\n", ret);

		ret = cpus_init(&dsts_invalid[i], srcs_invalid[i].ncpus);
		INTERR(ret, "cpus_init returned %d\n", ret);

		cpus_fill(&dsts_invalid[i], cpu_first.cpus[0]);
		last_idx = dsts_invalid[i].ncpus - 1;

		if (i == 0) {
			dsts_invalid[i].cpus[last_idx] =
				srcs_invalid[i].cpus[0];
			printf("src_cpus[0]: %d\n", srcs_invalid[i].cpus[0]);
		}
		if (i == 1) {
			dsts_invalid[i].cpus[last_idx] = max_id;
			printf("max_id: %d\n", max_id);
		}
		if (i == 2) {
			dsts_invalid[i].cpus[last_idx] = INT_MIN;
		}
		if (i == 3) {
			dsts_invalid[i].cpus[last_idx] = -1;
		}
		if (i == 4) {
			dsts_invalid[i].cpus[last_idx] = INT_MAX;
		}

		ret = ikc_cpu_map_init(&map_input[i],
				       srcs_invalid[i].ncpus);
		INTERR(ret, "ikc_cpu_map_init returned %d\n", ret);

		ret = ikc_cpu_map_copy(&map_input[i], &srcs_invalid[i],
				       &dsts_invalid[i]);
		INTERR(ret, "ikc_cpu_map_copy returned %d\n", ret);

		/* expected default ikc maps */
		ret = ikc_cpu_map_init(&map_after_set[i], cpus_mckernel.ncpus);
		INTERR(ret, "ikc_cpu_map_init returned %d\n", ret);

		for (j = 0; j < cpus_mckernel.ncpus; j++) {
			map_after_set[i].map[j].src_cpu = cpus_mckernel.cpus[j];
			map_after_set[i].map[j].dst_cpu = cpus_linux.cpus[0];
		}
	}

	/* Activate and check */
	for (i = 0; i < 4; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_create_os(0);
		INTERR(ret, "ihk_create_os returned %d\n", ret);

		ret = cpus_os_assign();
		INTERR(ret, "ihk_os_assign_cpu returned %d\n", ret);

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
	if (max_id != -1) {
		cpus_toggle(max_id, "on");
	}

	return ret;
}

