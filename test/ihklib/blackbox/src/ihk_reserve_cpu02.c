#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "cpu.h"
#include "params.h"
#include "linux.h"

int main(int argc, char **argv)
{
	int ret;
	int i;

	params_getopt(argc, argv);

	/* Prepare one with NULL and zero-clear others */

	const char *messages[] = {
		 "NULL",
		 "all",
		 "all + 1",
		 "all - 1",
		};

	struct cpus cpus_input[4] = { 0 };

	/* All of McKernel CPUs */
	for (i = 1; i < 4; i++) {
		ret = cpus_ls(&cpus_input[i]);
		INTERR(ret, "cpus_ls returned %d\n", ret);
	}

	/* Plus one */
	ret = cpus_push(&cpus_input[2],
			cpus_max_id(&cpus_input[2]) + 1);
	INTERR(ret, "cpus_push returned %d\n", ret);

	/* Minus one */
	ret = cpus_pop(&cpus_input[3], 1);
	INTERR(ret, "cpus_pop returned %d\n", ret);

	for (i = 1; i < 4; i++) {
		/* Spare two cpus for Linux */
		ret = cpus_shift(&cpus_input[i], 2);
		INTERR(ret, "cpus_shift returned %d\n", ret);
	}

	/* ncpus isn't zero but cpus is NULL */
	cpus_input[0].ncpus = 1;

	struct cpus cpu_after_reserve[4] = { 0 };

	/* All of McKernel CPUs */
	for (i = 0; i < 4; i++) {
		ret = cpus_ls(&cpu_after_reserve[i]);
		INTERR(ret, "cpus_ls returned %d\n", ret);
	}

	/* Minus one */
	ret = cpus_pop(&cpu_after_reserve[3], 1);
	INTERR(ret, "cpus_pop returned %d\n", ret);

	for (i = 1; i < 4; i++) {
		/* Spare two cpus for Linux */
		ret = cpus_shift(&cpu_after_reserve[i], 2);
		INTERR(ret, "cpus_shift returned %d\n", ret);
	}

	/* Empty */
	ret = cpus_shift(&cpu_after_reserve[0], cpu_after_reserve[0].ncpus);
	INTERR(ret, "cpus_shift returned %d\n", ret);

	/* Empty */
	ret = cpus_shift(&cpu_after_reserve[2], cpu_after_reserve[2].ncpus);
	INTERR(ret, "cpus_shift returned %d\n", ret);

	int ret_expected[] = {
		  -EFAULT,
		  0,
		  -EINVAL,
		  0,
	};

	struct cpus *cpus_expected[] = {
		  &cpu_after_reserve[0],
		  &cpu_after_reserve[1],
		  &cpu_after_reserve[2],
		  &cpu_after_reserve[3],
		};

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	/* Activate and check */
	for (i = 0; i < 4; i++) {
		START("test-case: cpus: %s\n", messages[i]);

		ret = ihk_reserve_cpu(0, cpus_input[i].cpus, cpus_input[i].ncpus);
		if (ret != ret_expected[i]) {
			cpus_dump(&cpus_input[i]);
		}

		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		if (cpus_expected[i]) {
			ret = cpus_check_reserved(cpus_expected[i]);
			OKNG(ret == 0, "reserved as expected\n");

			/* Clean up */
			ret = ihk_release_cpu(0, cpu_after_reserve[i].cpus,
					      cpu_after_reserve[i].ncpus);
			INTERR(ret, "ihk_release_cpu returned %d\n", ret);
		}
	}

	ret = 0;
 out:
	linux_rmmod(0);
	return ret;
}

