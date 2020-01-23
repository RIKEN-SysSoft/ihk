#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "params.h"
#include "linux.h"
#include <unistd.h>

#define INDEX_DUMMY -0x80000000

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
	int index_input[1] = { INDEX_DUMMY };
	int index_expected[1] = { 0 };

	for (i = 0; i < 1; i++) {
		int num_os_instances;

		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_create_os(0);
		INTERR(ret, "ihk_create_os returned %d\n", ret);

		ret = ihk_get_num_os_instances(0);
		INTERR(ret < 0, "ihk_get_num_os_instances returned %d\n", ret);
		num_os_instances = ret;

		ret = ihk_get_os_instances(0, index_input, num_os_instances);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);
		OKNG(index_input[i] == index_expected[i],
			"get os index as expected\n");

		ret = ihk_destroy_os(0, 0);
		INTERR(ret, "ihk_destroy_os returned %d\n", ret);
	}

	ret = 0;
out:
	linux_rmmod(0);
	return ret;
}
