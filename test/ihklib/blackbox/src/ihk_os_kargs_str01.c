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

const char param[] = "existence of IHK device file";
const char *values[] = {
	"without IHK device file",
	"with IHK device file",
};

const char *env_str[] = {
	"IHK_CPUS=12-35\0",
	"IHK_CPUS=12-35\0",
};

const int ret_expected[] = { -ENOENT, 0 };

#define NR_CASES (sizeof(values) / sizeof(values[0]))

int main(int argc, char **argv)
{
	int j;
	int ret;
	int subindex = -1;
	int opt;

	opterr = 0;
	while ((opt = getopt(argc, argv, "i:c")) != -1) {
		switch (opt) {
		case 'i':
			subindex = atoi(optarg);
			break;
		case 'c': /* clean up */
			ret = 0;
			goto out;
		default:
			INTERR(1, "unknown option %c\n", opt);
			break;
		}
	}
	opterr = 1;

	INTERR(subindex == -1, "-i <subindex> is not specified\n");

	struct cpus cpus[2] = { 0 };

	ret = cpus_init(&cpus[1], 24);
	INTERR(ret, "cpus_init returned %d\n", ret);

	for (j = 0; j < 24; j++) {
		cpus[1].cpus[j] = j + 12;
	}

	ARRAY_SIZE_CHECK(env_str, NR_CASES);
	ARRAY_SIZE_CHECK(ret_expected, NR_CASES);
	ARRAY_SIZE_CHECK(cpus, NR_CASES);

	START("test-case: %s: %s\n", param, values[subindex]);

	/* Precondition */
	if (subindex == 1) {
		ret = linux_insmod(0);
		INTERR(ret, "linux_insmod returned %d\n", ret);

	}

	ret = ihk_reserve_cpu_str(0, env_str[subindex], 1);

	OKNG(ret == ret_expected[subindex],
	     "return value: %d, expected: %d\n",
	     ret, ret_expected[subindex]);

	if (ret_expected[subindex] &&
	    ret_expected[subindex] != -ENOENT) {
		OKNG(ihk_get_num_reserved_cpus(0) == 0,
		     "no cpus reserved\n");
	}

	if (ret == 0) {
		ret = cpus_check_reserved(&cpus[subindex]);
		OKNG(ret == 0, "reserved as expected\n");
	}

	/* let the driver script check the kmsg */
	return 0;

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
