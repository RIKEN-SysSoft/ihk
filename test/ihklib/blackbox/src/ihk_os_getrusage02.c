#include <limits.h>
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

const char param[] = "os index";
const char *values[] = {
	"INT_MIN",
	"-1",
	"0",
	"1",
	"INT_MAX",
};

int main(int argc, char **argv)
{
	int ret;
	int i;
	int opt;
	int fd_in = -1, fd_out = -1;
	char *fn_in = NULL, *fn_out = NULL;

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

	int os_index_input[5] = {
		INT_MIN,
		-1,
		0,
		1,
		INT_MAX
	};

	int ret_expected[5] = {
		-ENOENT,
		-ENOENT,
		0,
		-ENOENT,
		-ENOENT,
	};
	struct ihk_os_rusage ru_input_before[5] = { 0 };
	struct ihk_os_rusage ru_input_after[5] = { 0 };
	struct ihk_os_rusage ru_expected[5] = { 0 };

	ru_expected[2].memory_stat_rss[IHK_OS_PGSIZE_64KB] = PAGE_SIZE * 1024;

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	ret = cpus_reserve();
	INTERR(ret, "cpus_reserve returned %d\n", ret);

	ret = mems_reserve();
	INTERR(ret, "mems_reserve returned %d\n", ret);

	pid_t pid = -1;

	/* Activate and check */
	for (i = 0; i < 5; i++) {
		int wstatus;
		int message = 1;
		char cmd[4096];

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

		sprintf(cmd, "mmap %s %s", fn_in, fn_out);
		ret = user_fork_exec(cmd, &pid);
		INTERR(ret < 0, "user_fork_exec returned %d\n", ret);

		/* Wait until child is ready */
		ret = read(fd_out, &message, sizeof(int));
		INTERR(ret <= 0, "read returned %d, errno: %d\n",
		       ret, errno);

		ret = ihk_os_getrusage(os_index_input[i],
				&ru_input_before[i],
				sizeof(struct ihk_os_rusage));
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		INFO("rss64k: %ld\n",
		ru_input_before[i].memory_stat_rss[IHK_OS_PGSIZE_64KB]);

		ret = write(fd_in, &message, sizeof(int));
		INTERR(ret != sizeof(int),
		       "write returned %d\n", errno);

		ret = read(fd_out, &message, sizeof(int));
		INTERR(ret <= 0, "read returned %d, errno: %d\n",
		       ret, errno);

		ret = ihk_os_getrusage(os_index_input[i], &ru_input_after[i],
				       sizeof(struct ihk_os_rusage));
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		INFO("rss64k: %ld\n",
		     ru_input_after[i].memory_stat_rss[IHK_OS_PGSIZE_64KB]);

		ret = write(fd_in, &message, sizeof(int));
		INTERR(ret != sizeof(int),
		       "write returned %d\n", errno);

		ret = waitpid(pid, &wstatus, 0);
		INTERR(ret < 0, "waitpid returned %d\n", errno);
		pid = -1;

		close(fd_in);
		close(fd_out);

		if (ret_expected[i] == 0) {
			unsigned long rss64k =
			ru_input_after[i].memory_stat_rss[IHK_OS_PGSIZE_64KB] -
			ru_input_before[i].memory_stat_rss[IHK_OS_PGSIZE_64KB];

			unsigned long rss64k_expected =
			ru_expected[i].memory_stat_rss[IHK_OS_PGSIZE_64KB];

			OKNG(rss64k >= rss64k_expected &&
			     rss64k <= rss64k_expected * 1.1,
			     "rss[64K]: %lu, expected: %lu\n",
			     rss64k, rss64k_expected);
		}
		else {
			OKNG(!memcmp(&ru_input_before[i], &ru_expected[i],
				     sizeof(struct ihk_os_rusage)) &&
			     !memcmp(&ru_input_after[i], &ru_expected[i],
				     sizeof(struct ihk_os_rusage)),
			     "output buffers are untouched\n");
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
