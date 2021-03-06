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
	int ret;
	int i;
	int offlined_cpu = -1;
	int unreserved_cpu = -1;

	params_getopt(argc, argv);

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	struct cpus cpu_unreserved = { 0 };
	struct cpus cpu_last = { 0 };
	struct cpus cpus_input[5] = {{ 0 }};

	ret = _cpus_ls(&cpu_last, "online", 2, -1);
	INTERR(ret, "_cpus_ls returned %d\n", ret);

	ret = cpus_shift(&cpu_last, cpu_last.ncpus - 1);
	INTERR(ret, "cpus_shift returned %d\n", ret);

	offlined_cpu = cpu_last.cpus[0];

	ret = cpus_toggle(offlined_cpu, "off");
	INTERR(ret, "cpus_toggle returned %d\n", ret);

	struct cpus cpus = { 0 };

	ret = _cpus_ls(&cpu_unreserved, "online", 2, -1);
	INTERR(ret, "_cpus_ls returned %d\n", ret);

	/* the 2nd last cpu */
	ret = cpus_shift(&cpu_unreserved, cpu_unreserved.ncpus - 1);
	INTERR(ret, "cpus_shift returned %d\n", ret);
	unreserved_cpu = cpu_unreserved.cpus[0];

	ret = _cpus_ls(&cpus, "online", 2, -1);
	INTERR(ret, "_cpus_ls returned %d\n", ret);

	ret = cpus_pop(&cpus, 1);
	INTERR(ret, "cpus_shift returned %d\n", ret);

	INFO("reserved cpu:\n");
	cpus_dump(&cpus);
	INFO("unreserved cpu: %d\n", unreserved_cpu);
	INFO("offlined cpu: %d\n", offlined_cpu);

	ret = ihk_reserve_cpu(0, cpus.cpus, cpus.ncpus);
	INTERR(ret, "ihk_reserve_cpu returned %d\n", ret);

	for (i = 0; i < 5; i++) {
		int push_id;

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

		ret = cpus_reserved(&cpus_input[i]);
		INTERR(ret, "cpus_reserved returned %d\n", ret);

		ret = cpus_pop(&cpus_input[i], 1);
		INTERR(ret, "cpus_shift returned %d\n", ret);

		ret = cpus_push(&cpus_input[i], push_id);
		INTERR(ret, "cpus_push returned %d\n", ret);
	}

	struct cpus cpus_after_assign[5] = {{ 0 }};

	for (i = 0; i < 5; i++) {
		ret = cpus_reserved(&cpus_after_assign[i]);
		INTERR(ret, "cpus_reserved returned %d\n", ret);

		/* Empty */
		ret = cpus_shift(&cpus_after_assign[i],
				cpus_after_assign[i].ncpus);
		INTERR(ret, "cpus_shift returned %d\n", ret);
	}

	int ret_expected[] = {
		-EINVAL,
		-EINVAL,
		-EINVAL,
		-EINVAL,
		-EINVAL,
	};

	struct cpus *cpus_expected[] = {
		  &cpus_after_assign[0],
		  &cpus_after_assign[1],
		  &cpus_after_assign[2],
		  &cpus_after_assign[3],
		  &cpus_after_assign[4],
	};

	ret = ihk_create_os(0);
	INTERR(ret, "ihk_create_os returned %d\n", ret);

	/* Activate and check */
	for (i = 0; i < 5; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_os_assign_cpu(0, cpus_input[i].cpus,
					cpus_input[i].ncpus);
		if (ret != ret_expected[i]) {
			cpus_dump(&cpus_input[i]);
		}

		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		if (cpus_expected[i]) {
			ret = cpus_check_assigned(cpus_expected[i]);
			OKNG(ret == 0, "assigned as expected\n");
		}
	}

	ret = 0;
 out:
	if (ihk_get_num_os_instances(0)) {
		ihk_destroy_os(0, 0);
	}
	cpus_release();
	linux_rmmod(0);
	cpus_toggle(offlined_cpu, "on");
	return ret;
}

