#include <limits.h>
#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "cpu.h"
#include "mem.h"
#include "os.h"
#include "params.h"
#include "linux.h"

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
	char fn[4096];

	sprintf(fn, "%s/%s/kernel/mckernel.img",
		QUOTE(WITH_MCK), QUOTE(BUILD_TARGET));

	params_getopt(argc, argv);

	int os_index_input[] = {
		INT_MIN,
		-1,
		0,
		1,
		INT_MAX
	};

	int ret_expected[] = {
		-ENOENT,
		-ENOENT,
		0,
		-ENOENT,
		-ENOENT,
	};

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	ret = cpus_reserve();
	INTERR(ret, "cpus_reserve returned %d\n", ret);

	ret = mems_reserve();
	INTERR(ret, "mems_reserve returned %d\n", ret);

	/* Activate and check */
	for (i = 0; i < 5; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_create_os(0);
		INTERR(ret, "ihk_create_os returned %d\n", ret);

		ret = cpus_os_assign();
		INTERR(ret, "cpus_os_assign returned %d\n", ret);

		ret = mems_os_assign();
		INTERR(ret, "mems_os_assign returned %d\n", ret);

		ret = ihk_os_load(os_index_input[i], fn);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		/* Clean up */
		ret = cpus_os_release();
		INTERR(ret, "cpus_os_assign returned %d\n", ret);

		ret = mems_os_release();
		INTERR(ret, "mems_os_assign returned %d\n", ret);

		ret = ihk_destroy_os(0, 0);
		INTERR(ret, "ihk_destroy_os returned %d\n", ret);
	}

	ret = 0;
 out:
	if (ihk_get_num_os_instances(0)) {
		ihk_destroy_os(0, 0);
	}
	cpus_release();
	mems_release();
	linux_rmmod(1);

	return ret;
}
