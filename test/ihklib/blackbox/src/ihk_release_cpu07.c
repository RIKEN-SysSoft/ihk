#include <limits.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "cpu.h"
#include "params.h"
#include "linux.h"

const char param[] = "cpus";
const char *values[] = {
	"unreserved CPU",
	"offlined CPU",
	"INT_MIN",
	"-1",
	"INT_MAX",
};

int main(int argc, char **argv)
{
	int ret = 0;
	int i;
	int offlined_cpu = -1;
	int unreserved_cpu = -1;

	params_getopt(argc, argv);

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	struct cpus cpu_unreserved = { 0 };
	struct cpus cpu_offlined = { 0 };
	struct cpus cpus_input[5] = {{ 0 }};
	struct cpus cpus_reserve_input[5] = {{ 0 }};

	/* e.g. 2-7 */
	ret = _cpus_ls(&cpu_offlined, "online", 2, -1);
	INTERR(ret, "_cpus_ls returned %d\n", ret);

	/* e.g. 7 */
	ret = cpus_shift(&cpu_offlined, cpu_offlined.ncpus - 1);
	INTERR(ret, "cpus_shift returned %d\n", ret);

	offlined_cpu = cpu_offlined.cpus[0];
	INFO("offlined: %d\n", offlined_cpu);

	ret = cpus_toggle(offlined_cpu, "off");
	INTERR(ret, "cpus_toggle returned %d\n", ret);

	/* e.g. 2-6 */
	ret = _cpus_ls(&cpu_unreserved, "online", 2, -1);
	INTERR(ret, "_cpus_ls returned %d\n", ret);

	/* e.g. 6 */
	ret = cpus_shift(&cpu_unreserved, cpu_unreserved.ncpus - 1);
	INTERR(ret, "cpus_shift returned %d\n", ret);
	unreserved_cpu = cpu_unreserved.cpus[0];

	for (i = 0; i < 5; i++) {
		int push_id;

		ret = _cpus_ls(&cpus_reserve_input[i], "online", 2, -1);
		INTERR(ret, "_cpus_ls returned %d\n", ret);

		/* e.g. 2-5 */
		ret = cpus_pop(&cpus_reserve_input[i], 1);
		INTERR(ret, "cpus_shift returned %d\n", ret);

		switch (i) {
		case 0:
			push_id = unreserved_cpu;
			break;
		case 1:
			push_id = offlined_cpu;
			break;
		case 2:
			push_id = INT_MIN;
			break;
		case 3:
			push_id = -1;
			break;
		case 4:
			push_id = INT_MAX;
			break;
		}

		ret = cpus_copy(&cpus_input[i], &cpus_reserve_input[i]);
		INTERR(ret, "cpus_copy returned %d\n", ret);

		ret = cpus_pop(&cpus_input[i], 1);
		INTERR(ret, "cpus_shift returned %d\n", ret);

		ret = cpus_push(&cpus_input[i], push_id);
		INTERR(ret, "cpus_push returned %d\n", ret);
	}

	struct cpus cpus_after_release[5] = {{ 0 }};

	for (i = 0; i < 5; i++) {
		ret = cpus_copy(&cpus_after_release[i], &cpus_reserve_input[i]);
		INTERR(ret, "cpus_reserved returned %d\n", ret);
	}

	int ret_expected[] = {
		-EINVAL,
		-EINVAL,
		-EINVAL,
		-EINVAL,
		-EINVAL,
	};

	struct cpus *cpus_expected[] = {
		  &cpus_after_release[0],
		  &cpus_after_release[1],
		  &cpus_after_release[2],
		  &cpus_after_release[3],
		  &cpus_after_release[4],
	};

	/* Activate and check */
	for (i = 0; i < 5; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		INFO("reserved:\n");
		cpus_dump(&cpus_reserve_input[i]);

		INFO("to release:\n");
		cpus_dump(&cpus_input[i]);

		ret = ihk_reserve_cpu(0, cpus_reserve_input[i].cpus,
				cpus_reserve_input[i].ncpus);
		INTERR(ret, "ihk_reserve_cpu returned %d\n", ret);

		ret = ihk_release_cpu(0, cpus_input[i].cpus,
				cpus_input[i].ncpus);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		if (cpus_expected[i]) {
			ret = cpus_check_reserved(cpus_expected[i]);
			OKNG(ret == 0, "reserved cpus are intact\n");
		}

		ret = cpus_release();
		INTERR(ret, "cpus_release returned %d\n", ret);
	}

	ret = 0;
 out:
	cpus_release();
	linux_rmmod(0);
	cpus_toggle(offlined_cpu, "on");
	return ret;
}

