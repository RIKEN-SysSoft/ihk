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

const char param[] = "cpuacct_stat_system and cpuacct_stat_user";
const char *values[] = {
	"user-mode 4s and kernel-mode 2s"
};

int main(int argc, char **argv)
{
	int ret;
	int i;
	int fd_in = -1, fd_out = -1;
	char *fn_in = NULL, *fn_out = NULL;
	int opt;
	long user_hz;

	ret = sysconf(_SC_CLK_TCK);
	INTERR(ret == -1, "sysconf returned %d\n", ret);
	user_hz = ret;

	params_getopt(argc, argv);

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

	int user_time[1] = { 4 };
	int kernel_time[1] = { 2 };
	int ret_expected[1] = { 0 };

	struct ihk_os_rusage ru_input_before[1] = { 0 };
	struct ihk_os_rusage ru_input_after[1] = { 0 };

	struct ihk_os_rusage ru_expected[1] = {
		{
			.cpuacct_stat_system = 2 * user_hz,
			.cpuacct_stat_user = 4 * user_hz,
		},
	};

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	ret = cpus_reserve();
	INTERR(ret, "cpus_reserve returned %d\n", ret);

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

		sprintf(cmd, "consume_cpu_time %s %s -u %d -k %d",
			fn_in, fn_out, user_time[i], kernel_time[i]);
		ret = user_fork_exec(cmd, &pid);
		INTERR(ret < 0, "user_fork_exec returned %d\n", ret);

		/* Wait until child is ready */
		ret = read(fd_out, &message, sizeof(int));
		INTERR(ret <= 0, "read returned %d, errno: %d\n",
		       ret, errno);

		ret = ihk_os_getrusage(0, &ru_input_before[i],
				sizeof(struct ihk_os_rusage));
		OKNG(ret == ret_expected[i], "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		INFO("user mode: %ld ticks\n",
			ru_input_before[i].cpuacct_stat_user);
		INFO("kernel mode: %ld ticks\n",
			ru_input_before[i].cpuacct_stat_system);

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

		INFO("user mode: %ld ticks\n",
				ru_input_after[i].cpuacct_stat_user);
		INFO("kernel mode: %ld ticks\n",
				ru_input_after[i].cpuacct_stat_system);

		ret = write(fd_in, &message, sizeof(int));
		INTERR(ret != sizeof(int), "write returned %d\n", errno);

		ret = waitpid(pid, &wstatus, 0);
		INTERR(ret < 0, "waitpid returned %d\n", errno);
		pid = -1;

		close(fd_in);
		close(fd_out);

		if (ret_expected[i] == 0) {
			unsigned long user_ticks =
			ru_input_after[i].cpuacct_stat_user -
			ru_input_before[i].cpuacct_stat_user;
			unsigned long kernel_ticks =
			ru_input_after[i].cpuacct_stat_system -
			ru_input_before[i].cpuacct_stat_system;

			unsigned long user_expected =
				ru_expected[i].cpuacct_stat_user;
			unsigned long kernel_expected =
				ru_expected[i].cpuacct_stat_system;

			OKNG(user_ticks >= user_expected &&
				user_ticks <= user_expected * 1.1,
				"user: %lu, expected: %lu\n",
				user_ticks, user_expected);
			OKNG(kernel_ticks >= kernel_expected &&
				kernel_ticks <= kernel_expected * 1.1,
				"kernel: %lu, expected: %lu\n",
				kernel_ticks, kernel_expected);
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
