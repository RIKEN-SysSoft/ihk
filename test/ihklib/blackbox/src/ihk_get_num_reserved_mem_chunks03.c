#include <limits.h>
#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "mem.h"
#include "params.h"
#include "linux.h"

const char param[] = "dev_index";
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

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	ret = mems_reserve();
	INTERR(ret, "mems_reserve returned %d\n", ret);

	int dev_index_input[] = {
		INT_MIN,
		-1,
		0,
		1,
		INT_MAX
	};

	struct mems mems_input[5] = { 0 };

	/* All of McKernel CPUs */
	for (i = 0; i < 5; i++) {
		ret = mems_reserved(&mems_input[i]);
		INTERR(ret, "mems_reserved returned %d\n", ret);
	}

	int ret_expected[] = {
		  -ENOENT,
		  -ENOENT,
		  mems_input[2].num_mem_chunks,
		  -ENOENT,
		  -ENOENT,
	};

	/* Activate and check */
	for (i = 0; i < 5; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_get_num_reserved_mem_chunks(dev_index_input[i]);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);
	}

	ret = 0;
 out:
	mems_release();
	linux_rmmod(0);
	return ret;
}
