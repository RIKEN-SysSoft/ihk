#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <ihklib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "util.h"
#include "okng.h"
#include "cpu.h"
#include "mem.h"
#include "os.h"
#include "params.h"
#include "linux.h"
#include "user.h"

#define NS_PER_SEC	1000000000UL

const char param[] = "cpuacct_usage_percpu";
const char *values[] = {
	"CPU#36 4s"
};

struct ihk_os_rusage ru_input_before[1];
struct ihk_os_rusage ru_input_after[1];

int ret_expected[1] = { 0 };

struct ihk_os_rusage ru_expected[1] = {
       {
		.cpuacct_usage_percpu[36] = 4 * NS_PER_SEC,
	},
};

int main(int argc, char **argv)
{
	int ret;
	int i;
	int fd_in = -1, fd_out = -1;
	char *fn_in = NULL, *fn_out = NULL;
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

	ret = _cpus_reserve(2, 37);
	INTERR(ret, "cpus_reserve returned %d\n", ret);

	ret = ihk_get_num_reserved_cpus(0);
	INTERR(ret < 37, "reserve more cpus\n");

	ret = mems_reserve();
	INTERR(ret, "mems_reserve returned %d\n", ret);

	pid_t pid = -1;
	int wstatus;
	int message = 1;
	char cmd[4096];

	/* Activate and check */
	for (i = 0; i < 1; i++) {

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

		ret = os_wait_for_status(IHK_STATUS_RUNNING);
		INTERR(ret, "os status didn't change to %d\n",
		       IHK_STATUS_RUNNING);

		fd_in = open(fn_in, O_RDWR);
		INTERR(fd_in == -1, "open returned %d\n", errno);

		fd_out = open(fn_out, O_RDWR);
		INTERR(fd_out == -1, "open returned %d\n", errno);

		sprintf(cmd, "consume_cpu_time %s %s -u 4",
				fn_in, fn_out);
		INFO("executing %s/bin/mcexec taskset --cpu-list 36 %s/bin/%s...\n",
		     QUOTE(WITH_MCK), QUOTE(CMAKE_INSTALL_PREFIX), cmd);
		ret = _user_fork_exec(cmd, &pid, "", "taskset --cpu-list 36");
		INTERR(ret < 0, "user_fork_exec returned %d\n", ret);

		/* Wait until child is ready */
		ret = read(fd_out, &message, sizeof(int));
		INTERR(ret <= 0, "read returned %d, errno: %d\n",
		       ret, errno);

		ret = ihk_os_getrusage(0, &ru_input_before[i],
				sizeof(struct ihk_os_rusage));
		OKNG(ret == ret_expected[i], "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		INFO("CPU#36: %ld ns\n",
			ru_input_before[i].cpuacct_usage_percpu[36]);

		ret = write(fd_in, &message, sizeof(int));
		INTERR(ret != sizeof(int),
		       "write returned %d\n", errno);

		ret = read(fd_out, &message, sizeof(int));
		INTERR(ret <= 0, "read returned %d, errno: %d\n",
		       ret, errno);

		ret = ihk_os_getrusage(0, &ru_input_after[i],
				sizeof(struct ihk_os_rusage));
		OKNG(ret == ret_expected[i], "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		INFO("CPU#36: %ld ns\n",
			ru_input_after[i].cpuacct_usage_percpu[36]);

		ret = write(fd_in, &message, sizeof(int));
		INTERR(ret != sizeof(int), "write returned %d\n", errno);

		ret = waitpid(pid, &wstatus, 0);
		INTERR(ret < 0, "waitpid returned %d\n", errno);
		pid = -1;

		close(fd_in);
		close(fd_out);

		if (ret_expected[i] == 0) {
			unsigned long cpu36 =
			ru_input_after[i].cpuacct_usage_percpu[36] -
			ru_input_before[i].cpuacct_usage_percpu[36];

			unsigned long cpu36_expected =
				ru_expected[i].cpuacct_usage_percpu[36];

			OKNG(cpu36 >= cpu36_expected &&
				cpu36 <= cpu36_expected * 1.1,
				"cpu36: %lu, expected: %lu\n",
				cpu36, cpu36_expected);
		}

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
	linux_rmmod(1);

	return ret;
}
