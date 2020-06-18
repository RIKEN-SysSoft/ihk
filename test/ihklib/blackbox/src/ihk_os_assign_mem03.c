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


	struct mems mems_input[5] = {{ 0 }};

	for (i = 0; i < 5; i++) {
		ret = mems_reserved(&mems_input[i]);
		INTERR(ret, "mems_reserved returned %d\n", ret);
	}


	struct mems mems_after_assign[5] = {{ 0 }};
	struct mems mems_margin[5] = {{ 0 }};

	ret = mems_reserved(&mems_after_assign[2]);
	INTERR(ret, "mems_reserved returned %d\n", ret);

	ret = mems_copy(&mems_margin[2], &mems_after_assign[2]);
	INTERR(ret, "mems_copy returned %d\n", ret);

	mems_fill(&mems_margin[2], 4UL << 20);

	int ret_expected[] = {
		  -ENOENT,
		  -ENOENT,
		  0,
		  -ENOENT,
		  -ENOENT,
		};

	struct mems *mems_expected[] = {
		  NULL, /* don't care */
		  NULL, /* don't care */
		  &mems_after_assign[2],
		  NULL, /* don't care */
		  NULL, /* don't care */
		};

	/* Activate and check */
	for (i = 0; i < 5; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_os_assign_mem(os_index_input[i],
				      mems_input[i].mem_chunks,
				      mems_input[i].num_mem_chunks);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		if (mems_expected[i]) {
			ret = mems_check_assigned(mems_expected[i],
						  &mems_margin[i]);
			OKNG(ret == 0, "assigned as expected\n");

			/* Clean up */
			ret = mems_os_release();
			INTERR(ret, "mems_os_release returned %d\n", ret);
		}
	}

	ret = 0;
 out:
	if (ihk_get_num_os_instances(0)) {
		mems_os_release();
		ihk_destroy_os(0, 0);
	}
	mems_release();
	linux_rmmod(0);
	return ret;
}
