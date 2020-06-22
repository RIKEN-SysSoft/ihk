#include <limits.h>
#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "cpu.h"
#include "params.h"
#include "linux.h"

const char param[] = "num_cpus";
const char *values[] = {
	"INT_MIN",
	"-1",
	"0",
	"1",
	"# of reserved",
	"# of reserved - 1",
	"# of reserved + 1",
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

	ret = _cpus_reserve(2, -1);
	INTERR(ret, "cpus_reserve returned %d\n", ret);

	ret = ihk_create_os(0);
	INTERR(ret, "ihk_create_os returned %d\n", ret);

	struct cpus cpus_input[8] = {{ 0 }};
	struct cpus cpus_after_release[8] = {{ 0 }};

	/* Both Linux and McKernel cpus */
	for (i = 0; i < 8; i++) {
		ret = cpus_reserved(&cpus_input[i]);
		INTERR(ret, "cpus_reserved returned %d\n", ret);

		ret = cpus_reserved(&cpus_after_release[i]);
		INTERR(ret, "cpus_reserved returned %d\n", ret);
	}
	cpus_input[0].ncpus = INT_MIN;
	cpus_input[1].ncpus = -1;
	cpus_input[2].ncpus = 0;
	cpus_input[7].ncpus = INT_MAX;

	/* one */
	ret = cpus_pop(&cpus_input[3], cpus_input[3].ncpus - 1);
	INTERR(ret, "cpus_pop returned %d\n", ret);

	/* reserved - 1 */
	ret = cpus_shift(&cpus_input[5], 1);
	INTERR(ret, "cpus_pop returned %d\n", ret);

	/* reserved + 1 */
	ret = cpus_push(&cpus_input[6], cpus_max_id(&cpus_input[6]) + 1);
	INTERR(ret, "cpus_push returned %d\n", ret);

	/* cpus_after_release */
	/* one */
	ret = cpus_shift(&cpus_after_release[3], 1);
	INTERR(ret, "cpus_shift returned %d\n", ret);

	/* reserved */
	ret = cpus_shift(&cpus_after_release[4],
			 cpus_after_release[4].ncpus);
	INTERR(ret, "cpus_shift returned %d\n", ret);

	/* reserved - 1 */
	ret = cpus_pop(&cpus_after_release[5],
			 cpus_after_release[5].ncpus - 1);
	INTERR(ret, "cpus_pop returned %d\n", ret);

	int ret_expected[] = {
		 -EINVAL,
		 -EINVAL,
		 0,
		 0,
		 0,
		 0,
		 -EINVAL,
		 -EINVAL,
	};

	struct cpus *cpus_expected[] = {
		&cpus_after_release[0],
		&cpus_after_release[1],
		&cpus_after_release[2],
		&cpus_after_release[3],
		&cpus_after_release[4],
		&cpus_after_release[5],
		&cpus_after_release[6],
		&cpus_after_release[7],
	};


	/* Activate and check */
	for (i = 0; i < 8; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		ret = cpus_os_assign();
		INTERR(ret, "cpus_os_assign returned %d\n", ret);

		ret = ihk_os_release_cpu(0, cpus_input[i].cpus,
				    cpus_input[i].ncpus);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		if (cpus_expected[i]) {
			ret = cpus_check_assigned(cpus_expected[i]);
			OKNG(ret == 0, "released as expected\n");

			/* Clean up */
			ret = cpus_os_release();
			INTERR(ret, "cpus_os_release returned %d\n", ret);
		}
	}

	ret = 0;
 out:
	cpus_release();
	linux_rmmod(0);
	return ret;
}
