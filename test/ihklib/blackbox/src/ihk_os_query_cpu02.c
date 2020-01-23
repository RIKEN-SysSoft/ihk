#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "cpu.h"
#include "params.h"
#include "linux.h"

const char param[] = "cpus to store result";
const char *values[] = {
	"NULL",
	"assigned",
	"assigned + 1",
	"assigned - 1",
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

	/* All of McKernel CPUs */
	for (i = 1; i < 4; i++) {
		ret = cpus_reserved(&cpus_input[i]);
		INTERR(ret, "cpus_reserved returned %d\n", ret);
	}

	ret = cpus_push(&cpus_input[2],
			cpus_max_id(&cpus_input[2]) + 1);
	INTERR(ret, "cpus_push returned %d\n", ret);

	ret = cpus_shift(&cpus_input[3], 1);
	INTERR(ret, "cpus_shift returned %d\n", ret);

	struct cpus cpus_after_assign[4] = { 0 };

	for (i = 1; i < 4; i++) {
		ret = cpus_reserved(&cpus_after_assign[i]);
		INTERR(ret, "cpus_reserved returned %d\n", ret);
	}

	int ret_expected[] = {
		-EINVAL,
		0,
		-EINVAL,
		-EINVAL,
	};

	struct cpus *cpus_expected[] = {
		NULL, /* don't care */
		&cpus_after_assign[1],
		NULL,
		NULL,
	};

	ret = cpus_os_assign();
	INTERR(ret, "cpus_os_assign returned %d\n", ret);

	/* Activate and check */
	for (i = 0; i < 4; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_os_query_cpu(0, cpus_input[i].cpus,
				cpus_input[i].ncpus);
		OKNG(ret == ret_expected[i],
			"return value: %d, expected: %d\n",
			ret, ret_expected[i]);

		if (cpus_expected[i]) {
			ret = cpus_compare(&cpus_input[i], cpus_expected[i]);
			OKNG(ret == 0, "query result matches input\n");
		}

	}

	/* Clean up */
	ret = cpus_os_release();
	INTERR(ret, "cpu_os_release returned %d\n", ret);

	ret = 0;
 out:
	cpus_release();
	linux_rmmod(0);
	return ret;
}

