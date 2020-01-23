#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "cpu.h"
#include "params.h"
#include "linux.h"

const char param[] = "cpus";
const char *values[] = {
	"NULL",
	"all",
	"all + 1",
	"all - 1",
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
	struct cpus cpus_input[4] = { 0 };

	for (i = 1; i < 4; i++) {
		ret = cpus_reserved(&cpus_input[i]);
		INTERR(ret, "cpus_reserved returned %d\n", ret);
	}

	/* Plus one */
	ret = cpus_push(&cpus_input[2],
			cpus_max_id(&cpus_input[2]) + 1);
	INTERR(ret, "cpus_push returned %d\n", ret);

	/* Minus one */
	ret = cpus_pop(&cpus_input[3], 1);
	INTERR(ret, "cpus_pop returned %d\n", ret);

	/* ncpus isn't zero but cpus is NULL */
	cpus_input[0].ncpus = 1;

	struct cpus cpus_after_assign[4] = { 0 };

	for (i = 0; i < 4; i++) {
		ret = cpus_reserved(&cpus_after_assign[i]);
		INTERR(ret, "cpus_reserved returned %d\n", ret);
	}

	/* Minus one */
	ret = cpus_pop(&cpus_after_assign[3], 1);
	INTERR(ret, "cpus_pop returned %d\n", ret);

	/* Empty */
	ret = cpus_shift(&cpus_after_assign[0], cpus_after_assign[0].ncpus);
	INTERR(ret, "cpus_shift returned %d\n", ret);

	/* Empty */
	ret = cpus_shift(&cpus_after_assign[2], cpus_after_assign[2].ncpus);
	INTERR(ret, "cpus_shift returned %d\n", ret);

	int ret_expected[] = {
		  -EFAULT,
		  0,
		  -EINVAL,
		  0,
	};

	struct cpus *cpus_expected[] = {
		  &cpus_after_assign[0],
		  &cpus_after_assign[1],
		  &cpus_after_assign[2],
		  &cpus_after_assign[3],
	};

	/* Activate and check */
	for (i = 0; i < 4; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_os_assign_cpu(0, cpus_input[i].cpus,
					cpus_input[i].ncpus);
		if (ret != ret_expected[i]) {
			cpus_dump(&cpus_input[i]);
		}

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

