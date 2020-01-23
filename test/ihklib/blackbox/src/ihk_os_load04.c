#include <errno.h>
#include <ihklib.h>
#include <stdlib.h>
#include <string.h>
#include "util.h"
#include "okng.h"
#include "cpu.h"
#include "mem.h"
#include "os.h"
#include "params.h"
#include "linux.h"

const char param[] = "fn";
const char *values[] = {
	"NULL",
	"Path of kernel image",
	"Path of non kernel-image"
};

int main(int argc, char **argv)
{
	int ret;
	int i;
	char fn[4096];

	char *fn_input[3] = {
		NULL,
		NULL,
		"/dev/zero"
	};

	params_getopt(argc, argv);

	int ret_expected[3] = {
		-EINVAL,
		0,
		-EINVAL,
	};

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	ret = cpus_reserve();
	INTERR(ret, "cpus_reserve returned %d\n", ret);

	ret = mems_reserve();
	INTERR(ret, "mems_reserve returned %d\n", ret);

	/* Activate and check */
	for (i = 0; i < 3; i++) {
		START("test-case: %s: %s\n", param, values[i]);
		if (i == 1) {
			sprintf(fn, "%s/%s/kernel/mckernel.img",
					QUOTE(WITH_MCK), QUOTE(BUILD_TARGET));

			fn_input[1] = strdup(fn);
		}

		ret = ihk_create_os(0);
		INTERR(ret, "ihk_create_os returned %d\n", ret);

		ret = cpus_os_assign();
		INTERR(ret, "cpus_os_assign returned %d\n", ret);

		ret = mems_os_assign();
		INTERR(ret, "mems_os_assign returned %d\n", ret);

		ret = ihk_os_load(0, fn_input[i]);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		/* Clean up */
		ret = cpus_os_release();
		INTERR(ret, "cpus_os_release returned %d\n", ret);

		ret = mems_os_release();
		INTERR(ret, "mems_os_release returned %d\n", ret);

		ret = ihk_destroy_os(0, 0);
		INTERR(ret, "ihk_destroy_os returned %d\n", ret);

	}

	ret = 0;
 out:
	if (fn_input[1] != NULL) {
		free(fn_input[1]);
	}
	if (ihk_get_num_os_instances(0)) {
		ihk_destroy_os(0, 0);
	}
	cpus_release();
	mems_release();
	linux_rmmod(1);

	return ret;
}
