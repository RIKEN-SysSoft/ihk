#include <limits.h>
#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "cpu.h"
#include "params.h"
#include "linux.h"

const char param[] = "user privilege";
const char *values[] = {
	"root",
};

int main(int argc, char **argv)
{
	int ret;
	int i;

	params_getopt(argc, argv);

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	ret = ihk_create_os(0);
	INTERR(ret, "ihk_create_os returned %d\n", ret);

	int ret_expected[1] = { 4 };

	/* Activate and check */
	for (i = 0; i < 1; i++) {
		START("test-case: %s: %s\n",  param, values[i]);

		ret = ihk_os_get_eventfd(0, IHK_OS_EVENTFD_TYPE_OOM);
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
