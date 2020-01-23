#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "cpu.h"
#include "params.h"
#include "linux.h"

const char param[] = "cpus";
const char *values[] = {
	 "include reserved but unassigned CPUs",
};

int main(int argc, char **argv)
{
	int ret;
	int i;

	params_getopt(argc, argv);

	struct cpus cpu_last = { 0 };

	ret = cpus_ls(&cpu_last);
	INTERR(ret, "cpus_ls returned %d\n", ret);

	int last = cpu_last.cpus[cpu_last.ncpus - 1];

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	struct cpus cpus = { 0 };

	ret = cpus_ls(&cpus);
	INTERR(ret, "cpus_ls returned %d\n", ret);

	ret = cpus_shift(&cpus, 2);
	INTERR(ret, "cpus_shift returned %d\n", ret);

	ret = ihk_reserve_cpu(0, cpus.cpus, cpus.ncpus);
	INTERR(ret, "ihk_reserve_cpu returned %d\n", ret);

	INFO("reserved cpu:\n");
	cpus_dump(&cpus);

	ret = ihk_create_os(0);
	INTERR(ret, "ihk_create_os returned %d\n", ret);

	struct cpus cpus_assign_input[1] = { 0 };
	struct cpus cpus_input[1] = { 0 };

	for (i = 0; i < 1; i++) {
		ret = cpus_reserved(&cpus_assign_input[i]);
		INTERR(ret, "cpus_reserved returned %d\n", ret);

		ret = cpus_pop(&cpus_assign_input[i], 1);
		INTERR(ret, "cpus_pop returned %d\n", ret);

		ret = cpus_reserved(&cpus_input[i]);
		INTERR(ret, "cpus_reserved returned %d\n", ret);
	}

	int ret_expected[] = {
		  -EINVAL,
	};

	struct cpus cpus_after_release[1] = { 0 };

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

		INFO("assigned cpu:\n");
		cpus_dump(&cpus_assign_input[i]);
		INFO("reserved but unassigned cpu: %d\n", last);

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
	cpus_release();
	linux_rmmod(0);
	return ret;
}

