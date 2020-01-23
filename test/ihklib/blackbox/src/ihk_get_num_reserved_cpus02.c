#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "cpu.h"
#include "params.h"
#include "linux.h"

const char *messages[] = {
	"0",
	"1",
	"all",
};

int main(int argc, char **argv)
{
	int ret;
	int i;

	params_getopt(argc, argv);

	/* Prepare one with NULL and zero-clear others */

	struct cpus cpus_input[] = {
		{
			.cpus = NULL,
			.ncpus = 0,
		},
		{ 0 },
		{ 0 },
	};

	/* All of McKernel CPUs */
	for (i = 1; i < 3; i++) {
		ret = cpus_ls(&cpus_input[i]);
		INTERR(ret, "cpus_ls returned %d\n", ret);

		/* Spare two cpus for Linux */
		ret = cpus_shift(&cpus_input[i], 2);
		INTERR(ret, "cpus_shift returned %d\n", ret);
	}

	/* First CPU */
	ret = cpus_shift(&cpus_input[1], cpus_input[1].ncpus - 1);
	INTERR(ret, "cpus_shift returned %d\n", ret);

	int ret_expected_reserve_cpu[] = { 0, 0, 0 };

	int ret_expected[] = { cpus_input[0].ncpus,
			       cpus_input[1].ncpus,
			       cpus_input[2].ncpus };
	struct cpus *cpus_expected[] = {
		  NULL, /* don't care */
		  &cpus_input[1],
		  &cpus_input[2],
		};

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	/* Activate and check */
	for (i = 0; i < 3; i++) {
		START("test-case: cpus: %s\n", messages[i]);

		ret = ihk_reserve_cpu(0, cpus_input[i].cpus,
				      cpus_input[i].ncpus);
		INTERR(ret != ret_expected_reserve_cpu[i],
		     "ihk_reserve_cpu returned %d\n", ret);

		ret = ihk_get_num_reserved_cpus(0);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		if (cpus_expected[i]) {
			ret = cpus_check_reserved(cpus_expected[i]);
			OKNG(ret == 0, "reserved as expected\n");

			/* Clean up */
			ret = ihk_release_cpu(0, cpus_input[i].cpus,
					      cpus_input[i].ncpus);
			INTERR(ret, "ihk_release_cpu returned %d\n", ret);
		}
	}

	ret = 0;
 out:
	linux_rmmod(0);
	return ret;
}

