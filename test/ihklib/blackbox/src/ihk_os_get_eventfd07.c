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

const char param[] = "eventfd type";
const char *values[] = {
	"INT_MIN",
	"-1",
	"INT_MAX",
};

int main(int argc, char **argv)
{
	int ret;
	int i;

	params_getopt(argc, argv);

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "insmod returned %d\n", ret);

	ret = ihk_create_os(0);
	INTERR(ret, "ihk_create_os returned %d\n", ret);

	int type_input[3] = {
		INT_MIN,
		-1,
		INT_MAX,
	};

	int ret_expected[3] = {
		-EINVAL,
		-EINVAL,
		-EINVAL,
	};

	/* Activate and check */
	for (i = 0; i < 3; i++) {
		START("test-case: %s: %s\n", param, values[i]);
		/* Precondition */

		ret = ihk_os_get_eventfd(0, type_input[i]);
		OKNG(ret == ret_expected[i],
				"return value: %d, expected: %d\n",
				ret, ret_expected[i]);
	}

	ret = 0;
 out:
	ihk_destroy_os(0, 0);
	linux_rmmod(0);

	return ret;
}
