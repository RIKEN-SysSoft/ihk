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
	INTERR(ret, "insmod returned %d\n", ret);

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
	struct cpus cpus_assigned[5] = { 0 };

	ret = cpus_init(&cpus_input[2], ihk_get_num_reserved_cpus(0));
	INTERR(ret, "cpus_init returned %d\n", ret);

	/* only query with os index of 0 succeeds */
	ret = cpus_reserved(&cpus_assigned[2]);
	INTERR(ret, "cpus_reserved returned %d\n", ret);

	int ret_expected[] = {
		-ENOENT,
		-ENOENT,
		0,
		-ENOENT,
		-ENOENT,
	};

	struct cpus *cpus_expected[] = {
		NULL,
		NULL,
		&cpus_assigned[2],
		NULL,
		NULL,
	};

	/* Activate and check */
	for (i = 0; i < 5; i++) {
		int ncpus;

		START("test-case: %s: %s\n", param, values[i]);

		ret = cpus_os_assign();
		INTERR(ret, "cpus_os_assign returned %d\n", ret);

		ret = ihk_os_get_num_assigned_cpus(0);
		INTERR(ret < 0,
			"ihk_os_get_num_assigned_cpus returned %d\n", ret);
		ncpus = ret;

		ret = ihk_os_query_cpu(os_index_input[i], cpus_input[i].cpus,
				ncpus);
		OKNG(ret == ret_expected[i],
			"return value: %d, expected: %d\n",
			ret, ret_expected[i]);

		if (cpus_expected[i]) {
			ret = cpus_compare(&cpus_input[i], cpus_expected[i]);
			OKNG(ret == 0, "query result matches assigned\n");
		}

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
