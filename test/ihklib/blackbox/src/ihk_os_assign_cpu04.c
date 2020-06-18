#include <limits.h>
#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "cpu.h"
#include "params.h"
#include "linux.h"

const char param[] = "num_cpus";
const char *values[] = {
		 "INT_MIN",
		 "-1",
		 "0",
		 "all",
		 "all + 1",
		 "all - 1",
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

	ret = _cpus_reserve(2, -1);
	INTERR(ret, "cpus_reserve returned %d\n", ret);

	ret = ihk_create_os(0);
	INTERR(ret, "ihk_create_os returned %d\n", ret);

	struct cpus cpus_input[7] = {{ 0 }};

	/* Both Linux and McKernel cpus */
	for (i = 0; i < 7; i++) {
		ret = cpus_reserved(&cpus_input[i]);
		INTERR(ret, "cpus_reserved returned %d\n", ret);
	}

	/* Plus one */
	ret = cpus_push(&cpus_input[4],
			cpus_max_id(&cpus_input[4]) + 1);
	INTERR(ret, "cpus_push returned %d\n", ret);

	/* Minus one */
	ret = cpus_pop(&cpus_input[5], 1);
	INTERR(ret, "cpus_pop returned %d\n", ret);

	cpus_input[0].ncpus = INT_MIN;
	cpus_input[1].ncpus = -1;
	cpus_input[2].ncpus = 0;
	cpus_input[6].ncpus = INT_MAX;

	struct cpus cpus_after_assign[7] = {{ 0 }};

	ret = cpus_reserved(&cpus_after_assign[3]);
	INTERR(ret, "cpus_reserved returned %d\n", ret);

	/* Minus one */
	ret = cpus_reserved(&cpus_after_assign[5]);
	INTERR(ret, "cpus_reserved returned %d\n", ret);
	ret = cpus_pop(&cpus_after_assign[5], 1);
	INTERR(ret, "cpus_pop returned %d\n", ret);

	int ret_expected[] = {
		  -EINVAL,
		  -EINVAL,
		  0,
		  0,
		  -EINVAL,
		  0,
		  -EINVAL,
	};

	struct cpus *cpus_expected[] = {
		  NULL, /* don't care */
		  NULL, /* don't care */
		  &cpus_after_assign[2],
		  &cpus_after_assign[3],
		  NULL, /* don't care */
		  &cpus_after_assign[5],
		  NULL, /* don't care */
		};

	/* Activate and check */
	for (i = 0; i < 7; i++) {
		START("test-case: : %s\n", values[i]);

		ret = ihk_os_assign_cpu(0,
				      cpus_input[i].cpus, cpus_input[i].ncpus);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		if (cpus_expected[i]) {
			ret = cpus_check_assigned(cpus_expected[i]);
			OKNG(ret == 0, "assigned as expected\n");

			/* Clean up */
			ret = ihk_os_release_cpu(0, cpus_after_assign[i].cpus,
					      cpus_after_assign[i].ncpus);
			INTERR(ret, "ihk_os_release_cpu returned %d\n", ret);
		}
	}

	ret = 0;
 out:
	if (ihk_get_num_os_instances(0)) {
		ihk_destroy_os(0, 0);
	}
	cpus_release();
	linux_rmmod(0);
	return ret;
}
