#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "cpu.h"
#include "params.h"
#include "linux.h"

const char *messages[] = {
	"NULL",
	"# of entries: # of reserved",
};

int main(int argc, char **argv)
{
	int ret;
	int i;

	params_getopt(argc, argv);

	/* Prepare one with NULL and zero-clear others */

	struct cpus cpus_input_reserve_cpu[2] = { 0 };

	/* All of McKernel CPUs */
	for (i = 0; i < 2; i++) {
		ret = cpus_ls(&cpus_input_reserve_cpu[i]);
		INTERR(ret, "cpus_ls returned %d\n", ret);

		/* Spare two cpus for Linux */
		ret = cpus_shift(&cpus_input_reserve_cpu[i], 2);
		INTERR(ret, "cpus_shift returned %d\n", ret);
	}

	struct cpus cpus_input[2] = { 0 };

	ret = cpus_init(&cpus_input[1], cpus_input_reserve_cpu[1].ncpus);
	INTERR(ret, "cpus_init returned %d\n", ret);

	int ret_expected_reserve_cpu[] = { 0, 0 };

	int ret_expected_get_num_reserved_cpus[] = {
		 cpus_input_reserve_cpu[0].ncpus,
		 cpus_input_reserve_cpu[1].ncpus,
		};

	int ret_expected[] = {
		  -EINVAL,
		  0,
		};

	struct cpus *cpus_expected[] = {
		  NULL, /* don't care */
		  &cpus_input_reserve_cpu[1],
		};

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	/* Activate and check */
	for (i = 0; i < 2; i++) {
		START("test-case: cpus array passed: %s\n", messages[i]);

		ret = ihk_reserve_cpu(0, cpus_input_reserve_cpu[i].cpus,
				      cpus_input_reserve_cpu[i].ncpus);
		INTERR(ret != ret_expected_reserve_cpu[i],
		     "ihk_reserve_cpu returned %d\n", ret);

		ret = ihk_get_num_reserved_cpus(0);
		INTERR(ret != ret_expected_get_num_reserved_cpus[i],
		     "ihk_get_num_reserved_cpus returned %d\n", ret);

		ret = ihk_query_cpu(0, cpus_input[i].cpus,
				    cpus_input[i].ncpus);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		if (cpus_expected[i]) {
			ret = cpus_compare(&cpus_input[i], cpus_expected[i]);
			OKNG(ret == 0, "query result matches input\n");
		}

		/* Clean up */
		ret = ihk_release_cpu(0, cpus_input_reserve_cpu[i].cpus,
				      cpus_input_reserve_cpu[i].ncpus);
		INTERR(ret, "ihk_release_cpu returned %d\n", ret);
	}

	ret = 0;
 out:
	linux_rmmod(0);
	return ret;
}

