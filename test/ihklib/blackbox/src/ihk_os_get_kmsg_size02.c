#include <limits.h>
#include <errno.h>
#include <ihklib.h>
#include <ihk/ihklib_private.h>
#include "util.h"
#include "okng.h"
#include "params.h"
#include "linux.h"
#include <unistd.h>

const char param[] = "os_index";
const char *values[] = {
	"INT_MIN",
	"-1",
	"0",
	"1",
	"INT_MAX",
};

int main(int argc, char **argv)
{
	ssize_t ret;
	int i;

	params_getopt(argc, argv);

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %zd\n", ret);

	int os_index_input[] = {
		INT_MIN,
		-1,
		0,
		1,
		INT_MAX
	};

	ssize_t ret_expected[5] = {
		-ENOENT,
		-ENOENT,
		IHK_KMSG_SIZE,
		-ENOENT,
		-ENOENT,
	};

	for (i = 0; i < 5; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_create_os(0);
		INTERR(ret, "ihk_create_os returned %zd\n", ret);

		ret = ihk_os_get_kmsg_size(os_index_input[i]);
		OKNG(ret == ret_expected[i],
		     "return value: %zd, expected: %zd\n",
		     ret, ret_expected[i]);

		ret = ihk_destroy_os(0, 0);
		INTERR(ret, "ihk_destroy_os returned %zd\n", ret);
	}

	ret = 0;
out:
	linux_rmmod(0);
	return ret;
}

