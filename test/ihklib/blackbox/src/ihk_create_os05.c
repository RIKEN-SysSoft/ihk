#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "params.h"
#include "linux.h"
#include <unistd.h>

const char param[] = "user privilege";
const char *values[] = {
	 "root",
};

int main(int argc, char **argv)
{
	int ret;
	int i;

	params_getopt(argc, argv);

	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	int ret_expected[1] = { 0 };
	int ret_expected_os_instances[1] = { 1 };

	for (i = 0; i < 1; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_create_os(0);
		OKNG(ret == ret_expected[i],
		     "return value (os index when positive): %d, expected: %d\n",
		     ret, ret_expected[i]);

		ret = ihk_get_num_os_instances(0);
		OKNG(ret == ret_expected_os_instances[i],
		     "# of os instances: %d, expected: %d\n",
		     ret, ret_expected_os_instances[i]);
	}

	ret = 0;
out:
	linux_rmmod(0);
	return ret;
}

