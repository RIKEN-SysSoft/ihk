#include <stdlib.h>
#include <string.h>
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

const char param[] = "valid env";
const char *values[] = {
	"IHK_CPUS=12-35",
	"IHK_CPUS=24-35,12-23",
};

#define NR_CASES (sizeof(values) / sizeof(values[0]))

const char *env_str[] = {
	"IHK_CPUS=12-35\0"
#if NR_NUMA == 1
	"IHK_MEM=1G@0\0"
#elif FIRST_USER_NUMA == 4
	"IHK_MEM=1G@4,512M@5\0"
#else
	"IHK_MEM=1G@0,512M@1\0"
#endif
	"IHK_KARGS=hidos allow_oversubscribe\0",
	"IHK_CPUS=24-35,12-23\0"
#if NR_NUMA == 1
	"IHK_MEM=1G@0\0"
#elif FIRST_USER_NUMA == 4
	"IHK_MEM=1G@4,512M@5\0"
#else
	"IHK_MEM=1G@0,512M@1\0"
#endif
	"IHK_KARGS=hidos allow_oversubscribe\0",
};

const int ret_expected[] = { 0, 0 };
struct cpus cpus_expected[NR_CASES] = { { 0 } };

int main(int argc, char **argv)
{
	int i;
	int j;
	int ret;

	for (i = 0; i < NR_CASES; i++) {
		ret = cpus_init(&cpus_expected[i], 24);
		INTERR(ret, "cpus_init returned %d\n", ret);
	}

	for (j = 0; j < 24; j++) {
		cpus_expected[0].cpus[j] = j + 12;
	}

	for (j = 0; j < 12; j++) {
		cpus_expected[1].cpus[j] = j + 24;
	}

	for (; j < 24; j++) {
		cpus_expected[1].cpus[j] = j;
	}

	ARRAY_SIZE_CHECK(values, NR_CASES);
	ARRAY_SIZE_CHECK(env_str, NR_CASES);
	ARRAY_SIZE_CHECK(ret_expected, NR_CASES);
	ARRAY_SIZE_CHECK(cpus_expected, NR_CASES);

	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	for (i = 0; i < NR_CASES; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_reserve_cpu_str(0, env_str[i], 3);
		INTERR(ret, "ihk_reserve_cpu_str failed with %d\n", ret);

		ret = ihk_reserve_mem_str(0, env_str[i], 3);
		INTERR(ret, "ihk_reserve_mem_str failed with %d\n", ret);

		ret = ihk_create_os(0);
		INTERR(ret < 0, "ihk_reserve_mem_str failed with %d\n", ret);

		ret = ihk_os_assign_cpu_str(0, env_str[i], 3);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		ret = cpus_check_assigned(&cpus_expected[i]);
		OKNG(ret == 0, "assigned and reordered as expected\n");

		ret = mems_os_assign();
		INTERR(ret, "mems_os_assign failed with %d\n", ret);

		ret = cpus_os_release();
		INTERR(ret, "cpus_os_release failed with %d\n", ret);

		ret = mems_os_release();
		INTERR(ret, "mems_os_release failed with %d\n", ret);

		ret = ihk_destroy_os(0, 0);
		INTERR(ret, "ihk_destroy_os failed with %d\n", ret);

		ret = cpus_release();
		INTERR(ret, "cpus_release failed with %d\n", ret);

		ret = mems_release();
		INTERR(ret, "mems_release with %d\n", ret);
	}

 out:
	if (ihk_get_num_os_instances(0) > 0) {
		cpus_os_release();
		mems_os_release();
		ihk_destroy_os(0, 0);
	}
	cpus_release();
	mems_release();
	linux_rmmod(0);

	return ret;
}
