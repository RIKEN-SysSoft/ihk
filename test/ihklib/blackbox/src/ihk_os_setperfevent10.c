#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
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

const char param[] = "event type";
const char *values[] = {
	"\n\tA64FX specific events:\n"
	"\t(1) # of VFP operations\n"
	"\t(2) # of SVE operations / 4\n"
	"\t(3) # of WFI/WFE wait cycles\n"
	"\t(4) # of read transactions from the CMG memory\n"
	"\t(5) # of write transactions to the CMG memory",
};

/* See the following for the explanation of the following config values.
 * https://github.com/fujitsu/A64FX/blob/master/doc/A64FX_PMU_v1.1.pdf
 * "ARM Architecture Reference Manual Supplement - The Scalable Vector
 * Extension (SVE), for ARMv8-A"
 */

#define FP_FIXED_OPS_SPEC 0x80c1
/* Counts architecturally executed NEON and VFP operations.  The
 * event counter is incremented by the specified number of elements for
 * NEON operations or by 1 for FP operations, and by twice
 * those amounts for operations that would also be counted by
 * FP_FMA_SPEC.
 */

#define FP_SCALE_OPS_SPEC 0x80c0
/* Counts architecturally executed SVE arithmetic operations. This
 * event counter is incremented by 128 / (vector length by bits)
 * and by twice that amount for operations that would also be counted
 * by SVE_FP_FMA_SPEC.
 * Total number of individual floating-point operations performed by SVE
 * instruction is:
 * FP_SCALE_OPS_SPEC * VL / 128
 */

#define WFE_WFI_CYCLE 0x018e
/* Counts every cycle that the instruction unit is halted by the
 * WFE/WFI instruction.
 */

#define BUS_READ_TOTAL_MEM 0x0316
/* Counts read transactions from memory connected to the CMG It counts
 * all events caused in the measured CMG regardless of measured PE.
 */


#define BUS_WRITE_TOTAL_MEM 0x031e
/* Counts write transactions to memory connect to the CMG.  It counts
 * all events caused in the measured CMG regardless of measured PE.
 */

#define NEVENTS 5

int main(int argc, char **argv)
{
	int ret;
	int i, j;
	int fd_in = -1, fd_out = -1;
	char *fn_in = NULL, *fn_out = NULL;
	int opt;
	int excess;
	pid_t pid = -1;

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
			ret = -EINVAL;
			goto out;
		}
	}

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	/* Use only th 1st NUMA node */
	ret = _cpus_reserve(4, 2);
	INTERR(ret, "cpus_reserve returned %d\n", ret);

	struct mems mems = { 0 };

	ret = mems_ls(&mems);
	INTERR(ret, "mems_ls returned %d\n", ret);

	excess = mems.num_mem_chunks - 4;
	if (excess > 0) {
		ret = mems_shift(&mems, excess);
		INTERR(ret, "mems_shift returned %d\n", ret);
	}

	ret = mems_pop(&mems, mems.num_mem_chunks - 1);
	INTERR(ret, "mems_pop returned %d\n", ret);

	ret = ihk_reserve_mem(0, mems.mem_chunks,
			      mems.num_mem_chunks);
	INTERR(ret, "ihk_reserve_mem returned %d\n", ret);

	struct ihk_perf_event_attr attr_input[] = {
		{
		 .config = FP_FIXED_OPS_SPEC,
		 .disabled = 1,
		 .pinned = 0,
		 .exclude_user = 0,
		 .exclude_kernel = 1,
		 .exclude_hv = 1,
		 .exclude_idle = 0
		},
		{
		 .config = FP_SCALE_OPS_SPEC,
		 .disabled = 1,
		 .pinned = 0,
		 .exclude_user = 0,
		 .exclude_kernel = 1,
		 .exclude_hv = 1,
		 .exclude_idle = 0
		},
		{
		 .config = WFE_WFI_CYCLE,
		 .disabled = 1,
		 .pinned = 0,
		 .exclude_user = 0,
		 .exclude_kernel = 1,
		 .exclude_hv = 1,
		 .exclude_idle = 0
		},
		{
		 .config = BUS_READ_TOTAL_MEM,
		 .disabled = 1,
		 .pinned = 0,
		 .exclude_user = 0,
		 .exclude_kernel = 1,
		 .exclude_hv = 1,
		 .exclude_idle = 0
		},
		{
		 .config = BUS_WRITE_TOTAL_MEM,
		 .disabled = 1,
		 .pinned = 0,
		 .exclude_user = 0,
		 .exclude_kernel = 1,
		 .exclude_hv = 1,
		 .exclude_idle = 0
		}
	};

	int ret_expected[1] = { NEVENTS };

	unsigned long count_expected[1][NEVENTS] = {
		{
			      4000000, /* vfp */

			      /* sve, see the above comment */
			      (1UL << 10) * 2 * (128 / (double)512),

			      (2 * 1000 * 1000 * 1000), /* wfi */

			      /* read tx
			       * - str also fetches data
			       * - guessing one data packet brings 256 byte
			       */
			      ((1UL << 30) + (1UL << 10) * 8 * 4) / 256,

			      /* write tx */
			      ((1UL << 30) + (1UL << 10) * 8) / 256,
		}
	};

	/* Activate and check */
	for (i = 0; i < 1; i++) {
		int message = 1;
		char cmd[4096];
		unsigned long counts[NEVENTS];
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

		ret = ihk_os_setperfevent(0, attr_input, 5);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		fd_in = open(fn_in, O_RDWR);
		INTERR(fd_in == -1, "open returned %d\n", errno);

		fd_out = open(fn_out, O_RDWR);
		INTERR(fd_out == -1, "open returned %d\n", errno);

		sprintf(cmd, "LD_LIBRARY_PATH=%s:$LD_LIBRARY_PATH "
			"%s/bin/mcexec %s/bin/vfp_sve_wfi_mem -i %s -o %s",
			QUOTE(FCC_LD_LIBRARY_PATH),
			QUOTE(WITH_MCK),
			QUOTE(CMAKE_INSTALL_PREFIX),
			fn_in, fn_out);
		INFO("cmd: %s\n", cmd);

		ret = __user_fork_exec(cmd, &pid);
		INTERR(ret < 0, "user_fork_exec returned %d\n", ret);

		/* Wait until child is ready */
		INFO("waiting for child finishing initialization...\n");
		ret = read(fd_out, &message, sizeof(int));
		INTERR(ret <= 0, "read returned %d, errno: %d\n",
		       ret, errno);

		ret = ihk_os_perfctl(0, PERF_EVENT_ENABLE);
		INTERR(ret, "PERF_EVENT_ENABLE returned %d\n", ret);

		INFO("letting child perfom tests...\n");
		ret = write(fd_in, &message, sizeof(int));
		INTERR(ret != sizeof(int),
		       "write returned %d\n", errno);

		INFO("waiting for child finishing tests...\n");
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

		for (j = 0; j < NEVENTS; j++) {
			OKNG(counts[j] > count_expected[i][j] * 0.9 &&
			     counts[j] < count_expected[i][j] * 1.1,
			     "count: %ld, expected: %ld\n",
			     counts[j], count_expected[i][j]);
		}

		INFO("letting child exit...\n");
		ret = write(fd_in, &message, sizeof(int));
		INTERR(ret != sizeof(int), "write returned %d\n", errno);

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
