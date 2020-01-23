#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "cpu.h"
#include "mem.h"
#include "os.h"
#include "params.h"
#include "linux.h"

const char param[] = "cpus";
const char *values[] = {
	"include reserved but unassigned CPU(s)",
};

int main(int argc, char **argv)
{
	int ret;
	int i;

	params_getopt(argc, argv);

	struct cpus cpus_mckernel = { 0 };

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	ret = cpus_reserve();
	INTERR(ret, "cpus_reserve returned %d\n", ret);

	ret = mems_reserve();
	INTERR(ret, "mems_reserve returned %d\n", ret);

	/* reserve all but last */
	ret = cpus_reserved(&cpus_mckernel);
	INTERR(ret, "cpus_reserved returned %d\n", ret);

	ret = cpus_pop(&cpus_mckernel, 1);
	INTERR(ret, "cpus_shift returned %d\n", ret);

	struct ikc_cpu_map map_input[1] = { 0 };

	/* invalid ikc src */
	struct cpus srcs_invalid = { 0 };

	ret = cpus_reserved(&srcs_invalid);
	INTERR(ret, "cpus_reserved returned %d\n", ret);

	ret = cpus_shift(&srcs_invalid, 1);
	INTERR(ret, "cpus_shift returned %d\n", ret);

	/* dest for invalid ikc src */
	struct cpus cpu_first = { 0 };

	ret = cpus_ls(&cpu_first);
	INTERR(ret, "cpus_ls returned %d\n", ret);

	struct cpus dsts_invalid = { 0 };

	ret = cpus_init(&dsts_invalid, srcs_invalid.ncpus);
	INTERR(ret, "cpus_init returned %d\n", ret);

	cpus_fill(&dsts_invalid, cpu_first.cpus[0]);

	for (i = 0; i < 1; i++) {
		ret = ikc_cpu_map_init(&map_input[i],
				       srcs_invalid.ncpus);
		INTERR(ret, "ikc_cpu_map_init returned %d\n", ret);

		ret = ikc_cpu_map_copy(&map_input[i], &srcs_invalid,
				       &dsts_invalid);
		INTERR(ret, "ikc_cpu_map_copy returned %d\n", ret);
	}

	struct ikc_cpu_map map_after_set[1] = { 0 };

	/* ikc map when ihk_os_set_ikc_map isn't called */
	int j;

	struct cpus cpus_linux = { 0 };

	ret = cpus_ls(&cpus_linux);
	INTERR(ret, "cpus_ls returned %d\n", ret);

	ret = cpus_pop(&cpus_linux, cpus_linux.ncpus - 2);
	INTERR(ret, "cpus_pop returned %d\n", ret);

	ret = ikc_cpu_map_init(&map_after_set[0],
			       cpus_mckernel.ncpus);
	INTERR(ret, "ikc_cpu_map_init returned %d\n", ret);

	for (j = 0; j < cpus_mckernel.ncpus; j++) {
		map_after_set[0].map[j].src_cpu =
			cpus_mckernel.cpus[j];
		map_after_set[0].map[j].dst_cpu =
			cpus_linux.cpus[0];
	}

	int ret_expected[] = {
		  -EINVAL,
	};

	struct ikc_cpu_map *map_expected[] = {
		&map_after_set[0],
	};


	/* Activate and check */
	for (i = 0; i < 1; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_create_os(0);
		INTERR(ret, "ihk_create_os returned %d\n", ret);

		ret = ihk_os_assign_cpu(0, cpus_mckernel.cpus,
				cpus_mckernel.ncpus);
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

	return ret;
}

