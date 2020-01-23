#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <stdlib.h>
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
#include "msr.h"

const char param[] = "McKernel CPU";
const char *values[] = {
	"last one",
};

#define ADDR_EXT IMP_FJ_TAG_ADDRESS_CTRL_EL1

int main(int argc, char **argv)
{
	int ret;
	int i;
	int fd = -1;
	char fn[4096];

	params_getopt(argc, argv);

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	sprintf(fn, "%s/kmod/test_driver.ko", QUOTE(CMAKE_INSTALL_PREFIX));
	ret = _linux_insmod(fn);
	INTERR(ret, "linux_insmod_test_driver returned %d\n", ret);

	struct cpus cpus = { 0 };

	ret = cpus_ls(&cpus);
	INTERR(ret, "cpus_ls returned %d\n", ret);

	ret = cpus_shift(&cpus, 2);
	INTERR(ret, "cpus_shift returned %d\n", ret);

	ret = ihk_reserve_cpu(0, cpus.cpus, cpus.ncpus);
	INTERR(ret, "ihk_reserve_cpu returned %d\n", ret);

	int last_cpu = cpus.ncpus - 1;

	INFO("last_cpu: %d\n", last_cpu);

	ret = mems_reserve();
	INTERR(ret, "mems_reserve returned %d\n", ret);

	/* Activate and check */
	for (i = 0; i < 1; i++) {
		char cmd[4096];
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

		sprintf(cmd, "%s/bin/mcexec taskset --cpu-list %d "
			"%s/bin/read_write_cpu_register -c %d -a %d",
			QUOTE(WITH_MCK), last_cpu,
			QUOTE(CMAKE_INSTALL_PREFIX), last_cpu,
			ADDR_EXT);
		ret = system(cmd);
		wstatus = WEXITSTATUS(ret);

		INTERR(ret < 0 || wstatus != 0,
		       "system returned %d or invalid exit status: %d\n",
		       errno, wstatus);

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

	linux_kill_mcexec();

	if (fd != -1) {
		close(fd);
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
	_linux_rmmod("test_driver");
	linux_rmmod(0);

	return ret;
}
