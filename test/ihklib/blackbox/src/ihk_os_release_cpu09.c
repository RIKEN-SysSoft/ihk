#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "cpu.h"
#include "params.h"
#include "linux.h"

const char param[] = "cpus";
const char *values[] = {
	 "include unreserved and unassigned CPUs",
};

int main(int argc, char **argv)
{
	int ret;
	int i;

	int unreserved;

	params_getopt(argc, argv);

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	struct cpus cpu_unreserve = { 0 };
	struct cpus cpus_assign_input[1] = {{ 0 }};
	struct cpus cpus_input[1] = {{ 0 }};
	struct cpus cpus_after_release[1] = {{ 0 }};

	/* e.g. try to release 2-7 when 3-7 is reserved */
	ret = _cpus_ls(&cpu_unreserve, "online", 2, -1);
	INTERR(ret, "_cpus_ls returned %d\n", ret);

	ret = cpus_pop(&cpu_unreserve, cpu_unreserve.ncpus - 1);
	INTERR(ret, "cpus_pop returned %d\n", ret);

	unreserved = cpu_unreserve.cpus[0];

	struct cpus cpus_reserve_input = { 0 };

	/* e.g. reserve 3-7 */
	ret = _cpus_ls(&cpus_reserve_input, "online", 3, -1);
	INTERR(ret, "_cpus_ls returned %d\n", ret);

	ret = ihk_reserve_cpu(0, cpus_reserve_input.cpus,
			cpus_reserve_input.ncpus);
	INTERR(ret, "ihk_reserve_cpu returned %d\n", ret);

	ret = ihk_create_os(0);
	INTERR(ret, "ihk_create_os returned %d\n", ret);

	for (i = 0; i < 1; i++) {
		ret = cpus_reserved(&cpus_assign_input[i]);
		INTERR(ret, "cpus_reserved returned %d\n", ret);

		ret = cpus_pop(&cpus_assign_input[i], 1);
		INTERR(ret, "cpus_pop returned %d\n", ret);

		ret = cpus_reserved(&cpus_input[i]);
		INTERR(ret, "cpus_reserved returned %d\n", ret);

		ret = cpus_shift(&cpus_input[i], 2);
		INTERR(ret, "cpus_shift returned %d\n", ret);

		ret = cpus_push(&cpus_input[i], unreserved);
		INTERR(ret, "cpus_push returned %d\n", ret);
	}

	int ret_expected[] = {
		  -EINVAL,
	};

	/* all */
	for (i = 0; i < 1; i++) {
		ret = cpus_reserved(&cpus_after_release[i]);
		INTERR(ret, "cpus_reserved returned %d\n", ret);

		ret = cpus_pop(&cpus_after_release[i], 1);
		INTERR(ret, "cpus_pop returned %d\n", ret);
	}

	struct cpus *cpus_expected[] = {
		  &cpus_after_release[0],
	};

	/* Activate and check */
	for (i = 0; i < 1; i++) {
		START("test-case: : %s\n", values[i]);

		INFO("reserved:\n");
		cpus_dump(&cpus_reserve_input);

		INFO("assigned:\n");
		cpus_dump(&cpus_assign_input[i]);

		INFO("to release:\n");
		cpus_dump(&cpus_input[i]);

		ret = ihk_os_assign_cpu(0, cpus_assign_input[i].cpus,
				      cpus_assign_input[i].ncpus);
		INTERR(ret, "ihk_os_assign_cpu returned %d\n", ret);

		ret = ihk_os_release_cpu(0, cpus_input[i].cpus,
				      cpus_input[i].ncpus);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		ret = cpus_check_assigned(cpus_expected[i]);
		OKNG(ret == 0, "assigned cpus are intact\n");

		ret = cpus_os_release();
		INTERR(ret, "cpus_os_release returned %d\n", ret);
	}

	ret = 0;
 out:
	if (ihk_get_num_os_instances(0)) {
		cpus_os_release();
		ihk_destroy_os(0, 0);
	}
	cpus_release();
	linux_rmmod(0);
	return ret;
}

