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

const char param[] = "user mode and kernel mode";
const char *values[] = {
	"user-mode 256MB && kernel-mode 512MB"
};

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

	size_t usermode_size[1] = {
		256 * 1024 * 1024,
	};

	size_t kernelmode_size[1] = {
		512 * 1024 * 1024,
	};

	int ret_expected[1] = { 0 };

	struct ihk_os_rusage ru_input_before[1] = { 0 };
	struct ihk_os_rusage ru_input_after[1] = { 0 };

	struct ihk_os_rusage ru_expected[1] = {
		{
			.memory_max_usage = 256 * 1024 * 1024,
			.memory_kmem_usage = 512 * 1024 * 1024
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

		sprintf(cmd, "mmap %s %s -u %zu -k %zu",
			fn_in, fn_out, usermode_size[i], kernelmode_size[i]);
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

		INFO("user mode: %ld\n",
			ru_input_before[i].memory_max_usage);
		INFO("kernel mode: %ld\n",
			ru_input_before[i].memory_kmem_usage);

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

		INFO("user mode: %ld\n", ru_input_after[i].memory_max_usage);
		INFO("kernel mode: %ld\n", ru_input_after[i].memory_kmem_usage);

		ret = write(fd_in, &message, sizeof(int));
		INTERR(ret != sizeof(int), "write returned %d\n", errno);

		ret = waitpid(pid, &wstatus, 0);
		INTERR(ret < 0, "waitpid returned %d\n", errno);
		pid = -1;

		close(fd_in);
		close(fd_out);

		if (ret_expected[i] == 0) {
			unsigned long user_mem =
			ru_input_after[i].memory_max_usage -
			ru_input_before[i].memory_max_usage;
			unsigned long kernel_mem =
			ru_input_after[i].memory_kmem_usage -
			ru_input_before[i].memory_kmem_usage;

			unsigned long user_expected =
				ru_expected[i].memory_max_usage;
			unsigned long kernel_expected =
				ru_expected[i].memory_kmem_usage;

			OKNG(user_mem >= user_expected &&
				user_mem <= user_expected * 1.1,
				"user: %lu, expected: %lu\n",
				user_mem, user_expected);
			OKNG(kernel_mem >= kernel_expected &&
				kernel_mem <= kernel_expected * 1.1,
				"kernel: %lu, expected: %lu\n",
				kernel_mem, kernel_expected);
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
