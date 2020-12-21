#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <ihklib.h>
#include <ihk/ihklib_private.h>
#include "util.h"
#include "okng.h"
#include "cpu.h"
#include "mem.h"
#include "os.h"
#include "params.h"
#include "linux.h"

const char param[] = "dev_index";
const char *values[] = {
	"INT_MIN",
	"-1",
	"1",
	"INT_MAX",
};
const int dev_index[] = {
	INT_MIN,
	-1,
	1,
	INT_MAX
};
const char *env_str[4] = {
	"IHK_CPUS=12-35\0",
	"IHK_CPUS=12-35\0",
	"IHK_CPUS=12-35\0",
	"IHK_CPUS=12-35\0",
};
const int ret_expected[] = {
	-ENOENT,
	-ENOENT,
	-ENOENT,
	-ENOENT,
};
const int nr_cpus_expected[] = {
	0, 0, 0, 0
};

#define NR_CASES (sizeof(values) / sizeof(values[0]))

int main(int argc, char **argv)
{
	int i;
	int ret;

	params_getopt(argc, argv);

	ARRAY_SIZE_CHECK(values, NR_CASES);
	ARRAY_SIZE_CHECK(env_str, NR_CASES);
	ARRAY_SIZE_CHECK(ret_expected, NR_CASES);
	ARRAY_SIZE_CHECK(nr_cpus_expected, NR_CASES);

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	for (i = 0; i < NR_CASES; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_reserve_cpu_str(dev_index[i],
					  env_str[i], 1);

		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		ret = ihk_get_num_reserved_cpus(0); 
		OKNG(ret == nr_cpus_expected[i],
		     "# of cpus reserved: actual %d, expected %d\n",
		     ret, nr_cpus_expected[i]);

		/* clean up */
		cpus_release();
	}

	ret = 0;
 out:
	if (ihk_get_num_os_instances(0) > 0) {
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
