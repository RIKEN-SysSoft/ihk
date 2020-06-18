#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "cpu.h"
#include "params.h"
#include "linux.h"

const char param[] = "cpus array passed";
const char *values[] = {
	"NULL",
	"reserved",
	"reserved + 1",
	"reserved - 1",
};

int main(int argc, char **argv)
{
	int ret;
	int i;

	params_getopt(argc, argv);

	/* Prepare one with NULL and zero-clear others */

	struct cpus cpus_input_reserve_cpu[4] = {{ 0 }};

	for (i = 0; i < 4; i++) {
		ret = _cpus_ls(&cpus_input_reserve_cpu[i], "online", 2, -1);
		INTERR(ret, "_cpus_ls returned %d\n", ret);
	}

	struct cpus cpus_input[4] = {
		{ .ncpus = 1, .cpus = NULL },
		{ 0 },
		{ 0 },
		{ 0 },
	};

	/* All of McKernel CPUs */
	for (i = 1; i < 4; i++) {
		ret = _cpus_ls(&cpus_input[i], "online", 2, -1);
		INTERR(ret, "_cpus_ls returned %d\n", ret);
	}

	/* Plus one */
	ret = cpus_push(&cpus_input[2],
			cpus_max_id(&cpus_input[2]) + 1);
	INTERR(ret, "cpus_push returned %d\n", ret);

	/* Minus one */
	ret = cpus_pop(&cpus_input[3], 1);
	INTERR(ret, "cpus_pop returned %d\n", ret);

	int ret_expected_reserve_cpu[4] = { 0 };

	int ret_expected[] = {
		  -EFAULT,
		  0,
		  -EINVAL,
		  0
	};

	struct cpus cpus_after_release[4] = {{ 0 }};

	/* All of McKernel CPUs */
	for (i = 0; i < 4; i++) {
		ret = _cpus_ls(&cpus_after_release[i], "online", 2, -1);
		INTERR(ret, "_cpus_ls returned %d\n", ret);
	}

	/* Empty */
	ret = cpus_shift(&cpus_after_release[1], cpus_after_release[1].ncpus);
	INTERR(ret, "cpus_shift returned %d\n", ret);

	/* Last one */
	ret = cpus_shift(&cpus_after_release[3],
			 cpus_after_release[3].ncpus - 1);
	INTERR(ret, "cpus_shift returned %d\n", ret);

	struct cpus *cpus_expected[] = {
		  &cpus_after_release[0],
		  &cpus_after_release[1],
		  &cpus_after_release[2],
		  &cpus_after_release[3],
	};

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	/* Activate and check */
	for (i = 0; i < 4; i++) {
		START("test-case: %s: %s\n",
		      param, values[i]);

		ret = ihk_reserve_cpu(0, cpus_input_reserve_cpu[i].cpus,
				      cpus_input_reserve_cpu[i].ncpus);
		INTERR(ret != ret_expected_reserve_cpu[i],
		     "ihk_reserve_cpu returned %d\n", ret);

		ret = ihk_release_cpu(0, cpus_input[i].cpus,
				      cpus_input[i].ncpus);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		ret = cpus_check_reserved(cpus_expected[i]);
		OKNG(ret == 0, "released as expected\n");

		/* Clean up */
		if (cpus_after_release[i].ncpus > 0) {
			ret = ihk_release_cpu(0, cpus_after_release[i].cpus,
					      cpus_after_release[i].ncpus);
			INTERR(ret, "ihk_release_cpu returned %d\n", ret);
		}
	}

	ret = 0;
 out:
	linux_rmmod(0);
	return ret;
}

