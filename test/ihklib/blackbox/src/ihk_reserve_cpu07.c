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
	"offlined CPU",
	"INT_MIN",
	"-1",
	"INT_MAX",
};

int main(int argc, char **argv)
{
	int ret;
	int i;
	int offlined_cpu = -1;

	params_getopt(argc, argv);

	/* Precondition */
	struct cpus cpu_offlined = { 0 };
	struct cpus cpus_input[4] = {{ 0 }};

	ret = _cpus_ls(&cpu_offlined, "online", 2, -1);
	INTERR(ret, "_cpus_ls returned %d\n", ret);

	ret = cpus_shift(&cpu_offlined, cpu_offlined.ncpus - 1);
	INTERR(ret, "cpus_shift returned %d\n", ret);

	offlined_cpu = cpu_offlined.cpus[0];

	ret = cpus_toggle(offlined_cpu, "off");
	INTERR(ret, "cpus_toggle returned %d\n", ret);

	/* Note that smp_ihk_init() marks online CPUs in
	 * ihk_smp_cpus[]
	 */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	for (i = 0; i < 4; i++) {
		int push_id;

		ret = _cpus_ls(&cpus_input[i], "online", 2, -1);
		INTERR(ret, "_cpus_ls returned %d\n", ret);

		switch (i) {
		case 0:
			push_id = offlined_cpu;
			break;
		case 1:
			push_id = INT_MIN;
			break;
		case 2:
			push_id = -1;
			break;
		case 3:
			push_id = INT_MAX;
			break;
		}

		ret = cpus_pop(&cpus_input[i], 1);
		INTERR(ret, "cpus_shift returned %d\n", ret);

		ret = cpus_push(&cpus_input[i], push_id);
		INTERR(ret, "cpus_push returned %d\n", ret);
	}

	struct cpus cpus_after_reserve[4] = {{ 0 }};

	int ret_expected[] = {
		-EINVAL,
		-EINVAL,
		-EINVAL,
		-EINVAL,
	};

	struct cpus *cpus_expected[] = {
		  &cpus_after_reserve[0],
		  &cpus_after_reserve[1],
		  &cpus_after_reserve[2],
		  &cpus_after_reserve[3],
	};

	/* Activate and check */
	for (i = 0; i < 4; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_reserve_cpu(0, cpus_input[i].cpus,
				cpus_input[i].ncpus);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		if (cpus_expected[i]) {
			ret = cpus_check_reserved(cpus_expected[i]);
			OKNG(ret == 0, "reserved (or not) as expected\n");
		}

		if (ret_expected == 0) {
			ret = cpus_release();
			INTERR(ret, "cpus_release returned %d\n", ret);
		}
	}

	ret = 0;
 out:
	cpus_release();
	linux_rmmod(0);
	cpus_toggle(offlined_cpu, "on");
	return ret;
}

