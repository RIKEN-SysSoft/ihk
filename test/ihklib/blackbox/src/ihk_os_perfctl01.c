#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "mem.h"
#include "cpu.h"
#include "os.h"
#include "params.h"
#include "linux.h"
#include "user.h"
#include "perf.h"

const char param[] = "existence of os instance";
const char *values[] = {
	"without os instance",
	"with os instance",
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

	ret = mems_reserve();
	INTERR(ret, "mems_reserve returned %d\n", ret);

	struct ihk_perf_event_attr attr_input[2][1] = { 0 };

	for (i = 0; i < 2; i++) {
		attr_input[i]->config = ARMV8_PMUV3_PERFCTR_INST_RETIRED;
		attr_input[i]->disabled = 1;
		attr_input[i]->pinned = 0;
		attr_input[i]->exclude_user = 0;
		attr_input[i]->exclude_kernel = 1;
		attr_input[i]->exclude_hv = 1;
		attr_input[i]->exclude_idle = 1;
	}

	int ret_expected[2] = {
		-ENOENT,
		0,
	};

	unsigned long count_expected[2] = {
		1000000,
		1000000,
	};
	pid_t pid = -1;

	/* Activate and check */
	for (i = 0; i < 2; i++) {
		unsigned long counts = 0UL;
		int wstatus;

		START("test-case: %s: %s\n", param, values[i]);

		if (i == 1) {
			ret = ihk_create_os(0);
			INTERR(ret, "ihk_create_os returned %d\n", ret);

			ret = cpus_os_assign();
			INTERR(ret, "cpus_os_assign returned %d\n", ret);

			ret = mems_os_assign();
			INTERR(ret, "mems_os_assign returned %d\n", ret);

			ret = os_load();
			INTERR(ret, "os_load returned %d\n", ret);

			ret = os_kargs();
			INTERR(ret, "os_kargs returned %d\n", ret);

			ret = ihk_os_boot(0);
			INTERR(ret, "ihk_os_boot returned %d\n", ret);

			ret = ihk_os_setperfevent(0, attr_input[i], 1);
			INTERR(ret != 1,
			       "ihk_os_setperfevent returned %d\n", ret);
		}

		ret = ihk_os_perfctl(0, PERF_EVENT_ENABLE);
		OKNG(ret == ret_expected[i],
		     "PERF_EVENT_ENABLE return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		ret = user_fork_exec("nop", &pid);
		INTERR(ret < 0, "user_fork_exec returned %d\n", ret);

		ret = waitpid(pid, &wstatus, 0);
		INTERR(ret < 0, "waitpid returned %d\n", errno);
		pid = -1;

		ret = ihk_os_perfctl(0, PERF_EVENT_DISABLE);
		OKNG(ret == ret_expected[i],
		     "PERF_EVENT_DISABLE return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		if (i == 1) {
			ret = ihk_os_getperfevent(0, &counts, 1);
			INTERR(ret, "ihk_os_getperfevent returned %d\n",
			       ret);

			OKNG(counts >= count_expected[i] &&
			     counts < count_expected[i] * 1.1,
			     "event count (%ld) is within expected range\n",
			     counts);

			ret = ihk_os_perfctl(0, PERF_EVENT_DESTROY);
			INTERR(ret, "PERF_EVENT_DESTROY returned %d\n", ret);

			ret = ihk_os_shutdown(0);
			INTERR(ret, "ihk_os_shutdown returned %d\n", ret);

			ret = os_wait_for_status(IHK_STATUS_INACTIVE);
			INTERR(ret, "os status didn't change to %d\n",
			       IHK_STATUS_INACTIVE);

			ret = cpus_os_release();
			INTERR(ret, "cpus_os_release returned %d\n", ret);

			ret = mems_os_release();
			INTERR(ret, "mems_os_release returned %d\n", ret);

			ret = ihk_destroy_os(0, 0);
			INTERR(ret, "ihk_destroy_os returned %d\n", ret);
		}
	}

	ret = 0;
 out:
	if (pid != -1) {
		user_wait(&pid);
		linux_kill_mcexec();
	}
	if (ihk_get_num_os_instances(0)) {
		ihk_os_shutdown(0);
		os_wait_for_status(IHK_STATUS_INACTIVE);
		cpus_os_release();
		mems_os_release();
		ihk_destroy_os(0, 0);
	}
	cpus_release();
	mems_release();
	linux_rmmod(0);

	return ret;
}
