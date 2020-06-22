#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "cpu.h"
#include "params.h"
#include "linux.h"

const char param[] = "user privilege";
const char *values[] = {
	"non-root",
};

int main(int argc, char **argv)
{
	int ret;
	int i;
	int previleged = 0;

	params_getopt(argc, argv);

	struct cpus cpus_input[1] = {{ 0 }};
	struct cpus cpus_after_assign[1] = {{ 0 }};

	int ret_expected[] = { -EPERM };

	struct cpus *cpus_expected[1] = {
		&cpus_after_assign[0],
	};

	/* Parse additional options */
	int opt;

	while ((opt = getopt(argc, argv, "ir")) != -1) {
		switch (opt) {
		case 'i':
			previleged = 1;
			
			/* Precondition */
			ret = linux_insmod(0);
			INTERR(ret, "linux_insmod returned %d\n", ret);

			ret = cpus_reserve();
			INTERR(ret, "cpus_reserve returned %d\n", ret);

			ret = ihk_create_os(0);
			INTERR(ret, "ihk_create_os returned %d\n", ret);

			exit(0);
			break;
		case 'r':
			previleged = 1;

			/* Check there's no side effects */
			if (cpus_expected[0]) {
				ret = cpus_check_assigned(cpus_expected[0]);
				OKNG(ret == 0, "(not) assigned as expected\n");
			}

			/* Clean up */
			ret = ihk_destroy_os(0, 0);
			INTERR(ret, "ihk_destroy_os returned %d\n", ret);

			ret = cpus_release();
			INTERR(ret, "cpus_release returned %d\n", ret);

			ret = linux_rmmod(1);
			INTERR(ret, "rmmod returned %d\n", ret);
			exit(0);
			break;
		default: /* '?' */
			printf("unknown option %c\n", optopt);
			exit(1);
		}
	}

	/* Activate and check */
	for (i = 0; i < 1; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		/* cpus are dummy because we can't query */
		ret = cpus_push(&cpus_input[i], 0);
		INTERR(ret, "cpus_push returned %d\n", ret);

		ret = linux_wait_chmod(0);
		INTERR(ret, "device file mode didn't change to 0666\n");

		ret = ihk_os_assign_cpu(0, cpus_input[i].cpus,
					cpus_input[i].ncpus);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);
	}

	ret = 0;
 out:
	if (previleged) {
		if (ihk_get_num_os_instances(0)) {
			ihk_destroy_os(0, 0);
		}
		cpus_release();
		linux_rmmod(0);
	}
	return ret;
}
