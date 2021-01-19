#include <limits.h>
#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "cpu.h"
#include "params.h"
#include "linux.h"

const char param[] = "cpus";
const char *values[] = {
	"outside /sys/fs/cgroup/cpuset/pxkrmjobs.slice/cpuset.mems",
};

int main(int argc, char **argv)
{
	int ret;
	int i;

	params_getopt(argc, argv);

	struct cpus cpus_input[1] = { { 0 } };

	/* Both Linux and McKernel cpus */
	for (i = 0; i < 1; i++) {
		ret = _cpus_ls(&cpus_input[i], "online", 2, -1);
		INTERR(ret, "_cpus_ls returned %d\n", ret);
	}

	/* expecting cpu#11 */
	ret = cpus_push(&cpus_input[0],
			cpus_min_id(&cpus_input[0]) - 1);
	INTERR(ret, "cpus_push returned %d\n", ret);

	int ret_expected[] = {
		  -EINVAL,
	};

	struct cpus *cpus_expected[] = {
		  NULL, /* empty */
	};

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	/* Activate and check */
	for (i = 0; i < 1; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_reserve_cpu(0,
				      cpus_input[i].cpus, cpus_input[i].ncpus);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		ret = cpus_check_reserved(cpus_expected[i]);
		OKNG(ret == 0, "reserved as expected\n");

		/* Clean up */
		ret = cpus_release();
		INTERR(ret, "ihk_release_cpu returned %d\n", ret);
	}

	ret = 0;
 out:
	linux_rmmod(0);
	return ret;
}
