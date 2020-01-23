#include <limits.h>
#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "cpu.h"
#include "params.h"
#include "linux.h"

const char param[] = "os_index";
const char *values[] = {
	 "INT_MIN",
	 "-1",
	 "0",
	 "1",
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

	ret = ihk_create_os(0);
	INTERR(ret, "ihk_create_os returned %d\n", ret);

	int os_index_input[] = {
		 INT_MIN,
		 -1,
		 0,
		 1,
		 INT_MAX
	};

	struct cpus cpus_input[5] = { 0 };

	/* All of McKernel CPUs */
	for (i = 0; i < 5; i++) {
		ret = cpus_reserved(&cpus_input[i]);
		INTERR(ret, "cpus_reserved returned %d\n", ret);
	}


	struct cpus cpus_after_assign[5] = { 0 };

	ret = cpus_reserved(&cpus_after_assign[2]);
	INTERR(ret, "cpus_reserved returned %d\n", ret);

	int ret_expected[] = {
		  -ENOENT,
		  -ENOENT,
		  0,
		  -ENOENT,
		  -ENOENT,
	};

	struct cpus *cpus_expected[] = {
		  NULL, /* don't care */
		  NULL, /* don't care */
		  &cpus_after_assign[2],
		  NULL, /* don't care */
		  NULL, /* don't care */
	};

	/* Activate and check */
	for (i = 0; i < 5; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_os_assign_cpu(os_index_input[i],
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
	cpus_release();
	linux_rmmod(0);

	return ret;
}
