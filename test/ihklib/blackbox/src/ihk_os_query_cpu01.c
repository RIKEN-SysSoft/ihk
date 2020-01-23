#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "cpu.h"
#include "params.h"
#include "linux.h"

const char param[] = "existence of os instance";
const char *values[] = {
	"without os instance",
	"with os instance",
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

	/* All of McKernel CPUs */
	struct cpus cpus_input[2] = { 0 };
	struct cpus cpus_assigned[2] = { 0 };

	for (i = 0; i < 2; i++) {
		ret = cpus_reserved(&cpus_input[i]);
		INTERR(ret, "cpus_reserved returned %d\n", ret);

		ret = cpus_reserved(&cpus_assigned[i]);
		INTERR(ret, "cpus_reserved returned %d\n", ret);
	}

	int ret_expected_assign_cpu[] = { -ENOENT, 0 };
	int ret_expected_get_num_assigned_cpu[] = {
		-ENOENT,
		cpus_assigned[1].ncpus
	};
	int ret_expected[] = { -ENOENT, 0 };

	struct cpus *cpus_expected[] = {
		NULL,
		&cpus_assigned[1],
	};

	/* Activate and check */
	for (i = 0; i < 2; i++) {
		int ncpus;

		START("test-case: %s: %s\n", param, values[i]);

		/* Precondition */
		if (i == 1) {
			ret = ihk_create_os(0);
			INTERR(ret, "ihk_create_os returned %d\n", ret);
		}
		ret = ihk_os_assign_cpu(0, cpus_input[i].cpus,
				      cpus_input[i].ncpus);
		INTERR(ret != ret_expected_assign_cpu[i],
		       "ihk_os_assign_cpu returned %d\n", ret);

		ret = ihk_os_get_num_assigned_cpus(0);
		INTERR(ret != ret_expected_get_num_assigned_cpu[i],
		       "ihk_os_get_num_assigned_cpus returned %d\n", ret);
		ncpus = ret;

		ret = ihk_os_query_cpu(0, cpus_input[i].cpus, ncpus);
		OKNG(ret == ret_expected[i],
			"return value: %d, expected: %d\n",
			ret, ret_expected[i]);

		if (cpus_expected[i]) {
			ret = cpus_compare(&cpus_input[i], cpus_expected[i]);
			OKNG(ret == 0, "query result matches assigned\n");

			/* Clean up */
			ret = cpus_os_release();
			INTERR(ret, "ihk_release_cpu returned %d\n", ret);
		}
	}

	ret = 0;
 out:
	cpus_release();
	linux_rmmod(0);
	return ret;
}
