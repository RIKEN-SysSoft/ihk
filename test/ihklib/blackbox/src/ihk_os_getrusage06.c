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

const char param[] = "64K page and 2MB page";
const char *values[] = {
	"256MB memory using 64KB pages",
	"512MB memory using 2MB pages",
};

struct ihk_os_rusage ru_input_before[2];
struct ihk_os_rusage ru_input_after[2];

int main(int argc, char **argv)
{
	int ret;
	int i;
	int fd_in = -1, fd_out = -1;
	char *fn_in = NULL, *fn_out = NULL;
	int opt;

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

	enum ihk_os_pgsize rusage_pgsizes[2] = {
/* 64KB page is recorded to index of IHK_OS_PGSIZE_4KB with Fugaku workaround.
 * see 91146ac Make struct ihk_os_rusage compatible with mckernel_rusage (workaround for Fugaku)
 */
#if 1
		IHK_OS_PGSIZE_4KB,
#else
		IHK_OS_PGSIZE_64KB,
#endif
		IHK_OS_PGSIZE_2MB,
	};

	enum ihk_os_pgsize pgsizes[2] = {
		IHK_OS_PGSIZE_64KB,
		IHK_OS_PGSIZE_2MB,
	};

	size_t mem_size[2] = {
		256 * 1024 * 1024,
		512 * 1024 * 1024,
	};

	int ret_expected[2] = { 0 };

	struct ihk_os_rusage ru_expected[2] = {
/* 64KB page is recorded to index of IHK_OS_PGSIZE_4KB with Fugaku workaround.
 * see 91146ac Make struct ihk_os_rusage compatible with mckernel_rusage (workaround for Fugaku)
 */
#if 1
		{ .memory_stat_rss[IHK_OS_PGSIZE_4KB] = 256 * 1024 * 1024 },
#else
		{ .memory_stat_rss[IHK_OS_PGSIZE_64KB] = 256 * 1024 * 1024 },
#endif
		{ .memory_stat_rss[IHK_OS_PGSIZE_2MB] =  512 * 1024 * 1024 },
	};

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	ret = cpus_reserve();
	INTERR(ret, "cpus_reserve returned %d\n", ret);

	ret = mems_reserve();
	INTERR(ret, "mems_reserve returned %d\n", ret);

	pid_t pid = -1;

	/* Activate and check */
	for (i = 0; i < 2; i++) {
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

		sprintf(cmd, "mmap %s %s -p %d -u %zu",
				fn_in, fn_out, pgsizes[i], mem_size[i]);
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

		INFO("rss: %lu\n",
		     ru_input_before[i].memory_stat_rss[rusage_pgsizes[i]]);

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

		INFO("rss: %lu\n",
			ru_input_after[i].memory_stat_rss[rusage_pgsizes[i]]);

		ret = write(fd_in, &message, sizeof(int));
		INTERR(ret != sizeof(int),
		       "write returned %d\n", errno);

		ret = waitpid(pid, &wstatus, 0);
		INTERR(ret < 0, "waitpid returned %d\n", errno);
		pid = -1;

		close(fd_in);
		close(fd_out);

		if (ret_expected[i] == 0) {
			unsigned long rss =
				ru_input_after[i].memory_stat_rss[rusage_pgsizes[i]] -
				ru_input_before[i].memory_stat_rss[rusage_pgsizes[i]];

			unsigned long rss_expected =
				ru_expected[i].memory_stat_rss[rusage_pgsizes[i]];

			OKNG(rss >= rss_expected && rss <= rss_expected * 1.1,
				"rss: %lu, expected: %lu\n", rss, rss_expected);
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
