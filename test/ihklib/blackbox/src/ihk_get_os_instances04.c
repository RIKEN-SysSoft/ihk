#include <limits.h>
#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "params.h"
#include "linux.h"
#include <unistd.h>

#define INDEX_DUMMY -0x80000000

const char param[] = "num_os_instances";
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

	params_getopt(argc, argv);

	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	int num_os_instance_input[5] = {
		INT_MIN,
		-1,
		0,
		1,
		INT_MAX
	};
	int ret_expected[5] = {
		-EINVAL,
		-EINVAL,
		-EINVAL,
		0,
		-EINVAL,
	};
	int index_input[5][1] = {
		{ INDEX_DUMMY },
		{ INDEX_DUMMY },
		{ INDEX_DUMMY },
		{ INDEX_DUMMY },
		{ INDEX_DUMMY },
	};
	int index_expected[5][1] = {
		{ INDEX_DUMMY },
		{ INDEX_DUMMY },
		{ INDEX_DUMMY },
		{ 0 },
		{ INDEX_DUMMY },
	};

	for (i = 0; i < 5; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_create_os(0);
		INTERR(ret, "ihk_create_os returned %d\n", ret);

		ret = ihk_get_os_instances(0,
				index_input[i], num_os_instance_input[i]);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);
		OKNG(index_input[i][0] == index_expected[i][0],
		     "actual index: %d expected index: %d\n",
		     index_input[i][0], index_expected[i][0]);

		ret = ihk_destroy_os(0, 0);
		INTERR(ret, "ihk_destroy_os returned %d\n", ret);
	}

	ret = 0;
out:
	linux_rmmod(0);
	return ret;
}
