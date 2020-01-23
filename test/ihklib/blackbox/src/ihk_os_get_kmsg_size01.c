#include <errno.h>
#include <ihklib.h>
#include <ihk/ihklib_private.h>
#include "util.h"
#include "okng.h"
#include "params.h"
#include "linux.h"
#include <unistd.h>

const char param[] = "existence of OS instance";
const char *values[] = {
	"wihout OS instance",
	"with OS instance",
};

int main(int argc, char **argv)
{
	ssize_t ret;
	int i;

	params_getopt(argc, argv);

	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %zd\n", ret);

	ssize_t ret_expected[2] = {
		-ENOENT,
		IHK_KMSG_SIZE,
	};

	for (i = 0; i < 2; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		/* Precondition */
		if (i == 1) {
			ret = ihk_create_os(0);
			INTERR(ret, "ihk_create_os returned %zd\n", ret);
		}

		ret = ihk_os_get_kmsg_size(0);
		OKNG(ret == ret_expected[i],
		     "return value: %zd, expected: %zd\n",
		     ret, ret_expected[i]);
	}

out:
	if (ihk_get_num_os_instances(0)) {
		ihk_destroy_os(0, 0);
	}
	linux_rmmod(0);

	return ret;
}

