#include <limits.h>
#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "mem.h"
#include "params.h"
#include "linux.h"

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
	int ret;
	int i;

	params_getopt(argc, argv);

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	ret = mems_reserve();
	INTERR(ret, "mems_reserve returned %d\n", ret);

	ret = ihk_create_os(0);
	INTERR(ret, "ihk_create_os returned %d\n", ret);

	int os_index_input[] = {
		 INT_MIN,
		 -1,
		 0,
		 1,
		 INT_MAX
	};

	struct mems mems_expected[5] = { 0 };

	/* All of McKernel CPUs */
	for (i = 0; i < 5; i++) {
		ret = mems_reserved(&mems_expected[i]);
		INTERR(ret, "mems_reserved returned %d\n", ret);
	}

	int ret_expected[] = {
		  -ENOENT,
		  -ENOENT,
		  mems_expected[2].num_mem_chunks,
		  -ENOENT,
		  -ENOENT,
	};


	/* Activate and check */
	for (i = 0; i < 5; i++) {
		START("test-case: os_index: %s\n", values[i]);

		ret = mems_os_assign();
		INTERR(ret, "mems_os_assign returned %d\n", ret);

		ret = ihk_os_get_num_assigned_mem_chunks(os_index_input[i]);
		OKNG(ret == ret_expected[i],
			"return value: %d, expected: %d\n",
			ret, ret_expected[i]);

		ret = mems_os_release();
		INTERR(ret, "mems_os_release returned %d\n", ret);
	}

	ret = 0;
 out:
	mems_release();
	linux_rmmod(0);
	return ret;
}
