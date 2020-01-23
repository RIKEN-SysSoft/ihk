#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "cpu.h"
#include "params.h"
#include "linux.h"

const char param[] = "cpus";
const char *values[] = {
	"0",
	"reserved",
	"reserved + 1",
	"reserved - 1",
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

	ret = ihk_create_os(0);
	INTERR(ret, "ihk_create_os returned %d\n", ret);

	/* Prepare one with NULL and zero-clear others */
	struct cpus cpus_input[] = {
		{
			.cpus = NULL,
			.ncpus = 0,
		},
		{ 0 },
		{
			.cpus = NULL,
			.ncpus = 0,
		},
		{ 0 },
	};

	/* All of McKernel CPUs */
	for (i = 1; i < 4; i++) {
		ret = cpus_reserved(&cpus_input[i]);
		INTERR(ret, "cpus_reserved returned %d\n", ret);
	}

	/* Add an additional CPU */
	ret = cpus_push(&cpus_input[2], cpus_max_id(&cpus_input[2]) + 1);
	INTERR(ret, "cpus_push returned %d\n", ret);

	/* Exclude the first CPU */
	ret = cpus_shift(&cpus_input[3], 1);
	INTERR(ret, "cpus_shift returned %d\n", ret);

	struct cpus cpus_after_assign[4] = {0};

	ret = cpus_reserved(&cpus_after_assign[1]);
	INTERR(ret, "cpus_reserved returned %d\n", ret);

	ret = cpus_reserved(&cpus_after_assign[3]);
	INTERR(ret, "cpus_reserved returned %d\n", ret);
	ret = cpus_shift(&cpus_after_assign[3], 1);
	INTERR(ret, "cpus_shift returned %d\n", ret);

	int ret_expected_assign_cpu[] = {
		0,
		0,
		-EINVAL,
		0,
	};

	int ret_expected[] = {
		cpus_after_assign[0].ncpus,
		cpus_after_assign[1].ncpus,
		cpus_after_assign[2].ncpus,
		cpus_after_assign[3].ncpus,
	};

	/* Activate and check */
	for (i = 0; i < 4; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_os_assign_cpu(0, cpus_input[i].cpus,
				cpus_input[i].ncpus);
		INTERR(ret != ret_expected_assign_cpu[i],
		       "ihk_os_assign_cpu returned %d\n", ret);

		ret = ihk_os_get_num_assigned_cpus(0);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		/* Clean up */
		ret = cpus_os_release();
		INTERR(ret, "cpus_os_release returned %d\n", ret);
	}

	ret = 0;
 out:
	cpus_release();
	linux_rmmod(0);
	return ret;
}

