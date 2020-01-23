#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
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

const char param[] = "disable user / kernel mode";
const char *values[] = {
	"disable kernel mode",
	"disable user mode",
};

int main(int argc, char **argv)
{
	int ret;
	int i;
	int fd_in = -1, fd_out = -1;
	char *fn_in = NULL, *fn_out = NULL;
	pid_t pid = -1;

	params_getopt(argc, argv);

	/* Parse additional options */
	int opt;

	while ((opt = getopt(argc, argv, "i:o:")) != -1) {
		switch (opt) {
		case 'i':
			fn_in = optarg;
			break;
		case 'o':
			fn_out = optarg;
			break;
		default: /* '?' */
			printf("unknown option %c\n", optopt);
			exit(1);
		}
	}

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	ret = cpus_reserve();
	INTERR(ret, "cpus_reserve returned %d\n", ret);

	ret = mems_reserve();
	INTERR(ret, "mems_reserve returned %d\n", ret);

	struct ihk_perf_event_attr attr_input[2] = {
		{
		 .config = ARMV8_PMUV3_PERFCTR_ST_RETIRED,
		 .disabled = 1,
		 .pinned = 0,
		 .exclude_user = 0,
		 .exclude_kernel = 1,
		 .exclude_hv = 1,
		 .exclude_idle = 0
		},
		{
		 .config = ARMV8_PMUV3_PERFCTR_ST_RETIRED,
		 .disabled = 1,
		 .pinned = 0,
		 .exclude_user = 1,
		 .exclude_kernel = 0,
		 .exclude_hv = 1,
		 .exclude_idle = 0
		}
	};

	int ret_expected[2] = { 1, 1 };

#define TEST_SZARRAY (1UL << 28)

	unsigned long count_expected[2] = {
		TEST_SZARRAY / sizeof(long),
		TEST_SZARRAY / sizeof(long) * 2,
	};

	/* Activate and check */
	for (i = 0; i < 2; i++) {
		char cmd[4096];
		int message = 1;
		unsigned long counts[1];
		int wstatus;

		START("test-case: %s: %s\n", param, values[i]);

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

		fd_in = open(fn_in, O_RDWR);
		INTERR(fd_in == -1, "open returned %d\n", errno);

		fd_out = open(fn_out, O_RDWR);
		INTERR(fd_out == -1, "open returned %d\n", errno);

		if (i == 0) {
			sprintf(cmd,
				"str -i %s -o %s -k 0",
				fn_in, fn_out);
		} else if (i == 1) {
			sprintf(cmd,
				"str -i %s -o %s -k 1",
				fn_in, fn_out);
		}
		ret = user_fork_exec(cmd, &pid);
		INTERR(ret < 0, "user_fork_exec returned %d\n", ret);

		/* Wait until child is ready */
		ret = read(fd_out, &message, sizeof(int));
		INTERR(ret <= 0, "read returned %d, errno: %d\n",
		       ret, errno);

		/* Set calls ihk_mc_perfctr_reset */
		ret = ihk_os_setperfevent(0, &attr_input[i], 1);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		ret = ihk_os_perfctl(0, PERF_EVENT_ENABLE);
		INTERR(ret, "PERF_EVENT_ENABLE returned %d\n", ret);

		/* Let child consume resources */
		ret = write(fd_in, &message, sizeof(int));
		INTERR(ret != sizeof(int),
		       "write returned %d\n", errno);

		/* Wait until child consumes resources */
		ret = read(fd_out, &message, sizeof(int));
		INTERR(ret <= 0, "read returned %d, errno: %d\n",
		       ret, errno);

		ret = ihk_os_perfctl(0, PERF_EVENT_DISABLE);
		INTERR(ret, "PERF_EVENT_DISABLE returned %d\n", ret);

		ret = ihk_os_getperfevent(0, counts, 1);
		INTERR(ret, "ihk_os_getperfevent returned %d\n",
		       ret);

		ret = ihk_os_perfctl(0, PERF_EVENT_DESTROY);
		INTERR(ret, "PERF_EVENT_DESTROY returned %d\n", ret);

		OKNG(counts[0] >= count_expected[i] &&
		     counts[0] < count_expected[i] * 1.1,
		     "event count: %ld (%ld K), expected: %ld (%ld K)\n",
		     counts[0], counts[0] >> 10,
		     count_expected[i], count_expected[i] >> 10);

		/* Let child exit */
		ret = write(fd_in, &message, sizeof(int));
		INTERR(ret != sizeof(int),
		       "write returned %d\n", errno);

		ret = waitpid(pid, &wstatus, 0);
		INTERR(ret < 0, "waitpid returned %d\n", errno);
		pid = -1;

		close(fd_in);
		close(fd_out);

		ret = linux_kill_mcexec();
		INTERR(ret, "linux_kill_mcexec returned %d\n", ret);

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
