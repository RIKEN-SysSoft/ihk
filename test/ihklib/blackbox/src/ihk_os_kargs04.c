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

const char param[] = "kargs";
const char *values[] = {
	"NULL",
	"1023 Bytes",
	"1024 Bytes",
	"1025 Bytes",
};

int main(int argc, char **argv)
{
	int ret;
	int i;
	char *kargs = "hidos ksyslogd=0";
	int kargs_size[4] = {
		0,
		1023,
		1024,
		1025,
	};

	char *kargs_input[4] = { NULL };

	for (i = 1; i < 4; i++) {
		switch (i) {
		case 1:
			kargs_input[i] = calloc(1023, sizeof(char));
			break;
		case 2:
			kargs_input[i] = calloc(1024, sizeof(char));
			break;
		case 3:
			kargs_input[i] = calloc(1025, sizeof(char));
			break;
		default:
			kargs_input[i] = NULL;
			break;
		}

		if (kargs_input[i]) {
			memset(kargs_input[i], 'a', kargs_size[i] - 1);
			memcpy(kargs_input[i], kargs, strlen(kargs));
		}
	}

	params_getopt(argc, argv);

	int ret_expected[4] = {
		-EFAULT,
		0,
		0,
		0,
	};

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	ret = cpus_reserve();
	INTERR(ret, "cpus_reserve returned %d\n", ret);

	ret = mems_reserve();
	INTERR(ret, "mems_reserve returned %d\n", ret);

	/* Activate and check */
	for (i = 0; i < 4; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_create_os(0);
		INTERR(ret, "ihk_create_os returned %d\n", ret);

		ret = cpus_os_assign();
		INTERR(ret, "cpus_os_assign returned %d\n", ret);

		ret = mems_os_assign();
		INTERR(ret, "mems_os_assign returned %d\n", ret);

		ret = os_load();
		INTERR(ret, "os_load returned %d\n", ret);

		ret = ihk_os_kargs(0, kargs_input[i]);
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
	if (ihk_get_num_os_instances(0)) {
		ihk_destroy_os(0, 0);
	}

	for (i = 0; i < 4; i++) {
		if (kargs_input[i]) {
			free(kargs_input[i]);
			kargs_input[i] = NULL;
		}
	}
	cpus_release();
	mems_release();
	linux_rmmod(1);

	return ret;
}
