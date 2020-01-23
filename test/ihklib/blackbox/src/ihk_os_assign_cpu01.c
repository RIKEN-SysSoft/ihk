#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "cpu.h"
#include "params.h"
#include "linux.h"

const char param[] = "existence of os instance";
const char *values[] = {
	"before ihk_create_os()",
	"after ihk_create_os()",
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

	struct cpus cpus_input[2] = { 0 };

	for (i = 0; i < 2; i++) {
		ret = cpus_reserved(&cpus_input[i]);
		INTERR(ret, "cpus_reserved returned %d\n", ret);
	}

	int ret_expected[] = { -ENOENT, 0 };

	struct cpus cpus_after_assign[2] = { 0 };

	for (i = 1; i < 2; i++) {
		ret = cpus_reserved(&cpus_after_assign[i]);
		INTERR(ret, "cpus_reserved returned %d\n", ret);
	}

	struct cpus *cpus_expected[] = {
		NULL,
		&cpus_after_assign[1]
	};

	/* Activate and check */
	for (i = 0; i < 2; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_os_assign_cpu(0, cpus_input[i].cpus,
				      cpus_input[i].ncpus);
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

		/* Precondition */
		if (i == 0) {
			ret = ihk_create_os(0);
			INTERR(ret, "ihk_create_os returned %d\n", ret);
		}
	}

	ret = 0;
 out:
	cpus_release();
	linux_rmmod(0);

	return ret;
}
