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

const char param[] = "existence of OS instance";
const char *values[] = {
	"wihout OS instance",
	"with OS instance",
};

int main(int argc, char **argv)
{
	int ret;
	int i;
	int fd_in = -1, fd_out = -1;
	char *fn_in = NULL, *fn_out = NULL;

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

	int ret_expected[2] = {
		-ENOENT,
		0
	};

	struct ihk_os_rusage ru_result[2][2] = { 0 };
	struct ihk_os_rusage ru_expected[2] = { 0 };

	ru_expected[1].memory_stat_rss[IHK_OS_PGSIZE_64KB] = PAGE_SIZE * 1024;

	pid_t pid = -1;

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	ret = cpus_reserve();
	INTERR(ret, "cpus_reserve returned %d\n", ret);

	ret = mems_reserve();
	INTERR(ret, "mems_reserve returned %d\n", ret);

	/* Activate and check */
	for (i = 0; i < 2; i++) {
		int wstatus;
		int word = 1;
		char cmd[4096];
		int message;

		START("test-case: %s: %s\n", param, values[i]);

		/* Precondition */
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
		}

		ret = ihk_os_getrusage(0, &ru_result[i][0],
				sizeof(struct ihk_os_rusage));
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		INFO("rss64k: %lu\n",
		     ru_result[i][0].memory_stat_rss[IHK_OS_PGSIZE_64KB]);

		if (i == 1) {
			/* Let child consume resources */
			ret = write(fd_in, &word, sizeof(int));
			INTERR(ret != sizeof(int),
			       "write returned %d\n", errno);

			/* Wait until child consumes resources */
			ret = read(fd_out, &message, sizeof(int));
			INTERR(ret <= 0, "read returned %d, errno: %d\n",
			       ret, errno);

			ret = ihk_os_getrusage(0, &ru_result[i][1],
					       sizeof(struct ihk_os_rusage));
			OKNG(ret == ret_expected[i],
			     "return value: %d, expected: %d\n",
			     ret, ret_expected[i]);

			INFO("rss64k: %lu\n",
			     ru_result[i][1].memory_stat_rss[IHK_OS_PGSIZE_64KB]);

			/* Let child exit */
			ret = write(fd_in, &word, sizeof(int));
			INTERR(ret != sizeof(int),
			       "write returned %d\n", errno);

			ret = waitpid(pid, &wstatus, 0);
			INTERR(ret < 0, "waitpid returned %d\n", errno);
			pid = -1;

			close(fd_in);
			close(fd_out);

			ret = linux_kill_mcexec();
			INTERR(ret, "linux_kill_mcexec returned %d\n", ret);

			unsigned long rss64k =
				ru_result[i][1].memory_stat_rss[IHK_OS_PGSIZE_64KB] -
				ru_result[i][0].memory_stat_rss[IHK_OS_PGSIZE_64KB];

			unsigned long rss64k_expected =
				ru_expected[i].memory_stat_rss[IHK_OS_PGSIZE_64KB];
			OKNG(rss64k >= rss64k_expected &&
			     rss64k <= rss64k_expected * 1.1,
			     "rss[64K]: %lu, expected: %lu\n",
			     rss64k, rss64k_expected);
		} else {
			OKNG(!memcmp(&ru_result[i][0], &ru_expected[i],
				     sizeof(struct ihk_os_rusage)) &&
			     !memcmp(&ru_result[i][1], &ru_expected[i],
				     sizeof(struct ihk_os_rusage)),
			     "output buffers are untouched\n");
		}

		/* Clean up */
		if (ihk_get_num_os_instances(0)) {
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
	linux_rmmod(1);

	return ret;
}
