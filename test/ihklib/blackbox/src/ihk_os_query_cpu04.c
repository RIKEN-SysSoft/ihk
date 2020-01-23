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
	INTERR(ret, "insmod returned %d\n", ret);

	ret = cpus_reserve();
	INTERR(ret, "cpus_reserve returned %d\n", ret);

	ret = ihk_create_os(0);
	INTERR(ret, "ihk_create_os returned %d\n", ret);

	int ncpus;

	ncpus = ihk_get_num_reserved_cpus(0);
	INTERR(ncpus < 0, "ihk_get_num_reserved_cpus returned %d\n", ret);

	int ncpus_input[8] = {
		 INT_MIN,
		 -1,
		 0,
		 1,
		 ncpus, /* reserved */
		 ncpus - 1, /* reserved - 1 */
		 ncpus + 1, /* reserved + 1 */
		 INT_MAX,
	};

	struct cpus cpus_input[8] = { 0 };

	for (i = 0; i < 8; i++) {
		if (i == 0 || i == 1 || i == 2 || i == 7) {
			cpus_input[i].ncpus = ncpus_input[i];
		} else {
			ret = cpus_init(&cpus_input[i],
					ncpus_input[i]);
			INTERR(ret, "cpus_init returned %d\n", ret);
		}
	}

	struct cpus cpus_assigned[8] = { 0 };

	ret = cpus_reserved(&cpus_assigned[4]);
	INTERR(ret, "cpus_reserved returned %d\n", ret);

	int ret_expected[] = {
		-EINVAL,
		-EINVAL,
		-EINVAL,
		-EINVAL,
		0,
		-EINVAL,
		-EINVAL,
		-EINVAL,
	};

	struct cpus *cpus_expected[] = {
		NULL,
		NULL,
		NULL,
		NULL,
		&cpus_assigned[4],
		NULL,
		NULL,
		NULL,
	};

	/* Activate and check */
	for (i = 0; i < 8; i++) {
		START("test-case: num_cpus: %s\n", values[i]);

		ret = cpus_os_assign();
		INTERR(ret, "cpus_os_assign returned %d\n", ret);

		ret = ihk_os_query_cpu(0, cpus_input[i].cpus,
				cpus_input[i].ncpus);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		if (cpus_expected[i]) {
			ret = cpus_compare(&cpus_input[i], cpus_expected[i]);
			OKNG(ret == 0, "query result matches assigned\n");
		}

		ret = cpus_os_release();
		INTERR(ret, "cpus_os_release returned %d\n", ret);
	}

	ret = 0;
 out:
	cpus_release();
	linux_rmmod(0);
	return ret;
}
