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

const char param[] = "memory_numa_stat";
const char *values[] = {
	"256MB at the 1st node",
	"512MB at the 2nd node"
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
	pid_t pid = -1;
	int wstatus;
	int message = 1;
	char cmd[4096], mcexecopt[4096];

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

	struct cpus cpus_mckernel = { 0 };

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	ret = cpus_ls(&cpus_mckernel);
	INTERR(ret, "cpus_ls returned %d\n", ret);

	ret = cpus_shift(&cpus_mckernel, 2);
	INTERR(ret, "cpus_shift returned %d\n", ret);

	ret = ihk_reserve_cpu(0, cpus_mckernel.cpus, cpus_mckernel.ncpus);
	INTERR(ret, "ihk_reserve_cpu returned %d\n", ret);

	struct mems mems = { 0 };
	int excess;

	ret = mems_ls(&mems);
	INTERR(ret, "mems_ls returned %d\n", ret);

	excess = mems.num_mem_chunks - 4;
	if (excess > 0) {
		ret = mems_shift(&mems, excess);
		INTERR(ret, "mems_shift returned %d\n", ret);
	}

	INTERR(mems.num_mem_chunks < 2,
	       "# of NUMA nodes (%d) < 2\n", mems.num_mem_chunks);

	ret = ihk_reserve_mem(0, mems.mem_chunks,
			      mems.num_mem_chunks);
	INTERR(ret, "ihk_reserve_mem returned %d\n", ret);

	unsigned long size_input[] = { 256 << 20, 512 << 20 };
	int node_input[2] = { 0, 1 }; /* McKernel numbering */
	int ret_expected[2] = { 0 };

	/* Activate and check */
	for (i = 0; i < 2; i++) {

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

		/* use mcexec -m <NUMA-node-id> because MPOL_MF_STRICT
		 * isn't supported
		 */
		sprintf(cmd, "mmap %s %s -u %lu",
			fn_in, fn_out, size_input[i]);
		sprintf(mcexecopt, "-m %d", node_input[i]);
		ret = _user_fork_exec(cmd, &pid, mcexecopt);
		INTERR(ret < 0, "user_fork_exec returned %d\n", ret);

		/* Wait until child is ready */
		ret = read(fd_out, &message, sizeof(int));
		INTERR(ret <= 0, "read returned %d, errno: %d\n",
		       ret, errno);

		ret = ihk_os_getrusage(0, &ru_input_before[i],
				sizeof(struct ihk_os_rusage));
		OKNG(ret == ret_expected[i], "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		INFO("node#0: %lu\n",
			ru_input_before[i].memory_numa_stat[0]);
		INFO("node#1: %lu\n",
			ru_input_before[i].memory_numa_stat[1]);

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

		INFO("node#0: %lu\n", ru_input_after[i].memory_numa_stat[0]);
		INFO("node#1: %lu\n", ru_input_after[i].memory_numa_stat[1]);

		ret = write(fd_in, &message, sizeof(int));
		INTERR(ret != sizeof(int), "write returned %d\n", errno);

		ret = waitpid(pid, &wstatus, 0);
		INTERR(ret < 0, "waitpid returned %d\n", errno);
		pid = -1;

		close(fd_in);
		close(fd_out);

		if (ret_expected[i] == 0) {
			unsigned long diff =
			ru_input_after[i].memory_numa_stat[node_input[i]] -
			ru_input_before[i].memory_numa_stat[node_input[i]];

			OKNG(diff >= size_input[i] &&
			     diff <= size_input[i] * 1.1,
			     "memory_numa_stat[%d]: %lu, expected: %lu\n",
			     node_input[i], diff, size_input[i]);
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
