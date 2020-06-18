#include <limits.h>
#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "mem.h"
#include "params.h"
#include "linux.h"

const char param[] = "num_mem_chunks";
const char *values[] = {
	"INT_MIN",
	"-1",
	"0",
	"all",
	"all + 1",
	"all - 1",
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

	ret = _mems_reserve(4, 0.9, -1);
	INTERR(ret, "_mems_reserve returned %d\n", ret);

	ret = ihk_create_os(0);
	INTERR(ret, "ihk_create_os returned %d\n", ret);

	struct mems mems_input[7] = {{ 0 }};

	for (i = 0; i < 7; i++) {
		ret = mems_reserved(&mems_input[i]);
		INTERR(ret, "mems_reserved returned %d\n", ret);
	}

	/* Plus one */
	ret = mems_push(&mems_input[4],
			mems_input[4].mem_chunks[0].size,
			mems_input[4].mem_chunks[0].numa_node_number);
	INTERR(ret, "mems_push returned %d\n", ret);

	mems_dump(&mems_input[4]);

	/* Minus one */
	ret = mems_pop(&mems_input[5], 1);
	INTERR(ret, "mems_pop returned %d\n", ret);

	mems_input[0].num_mem_chunks = INT_MIN;
	mems_input[1].num_mem_chunks = -1;
	mems_input[2].num_mem_chunks = 0;
	mems_input[6].num_mem_chunks = INT_MAX;

	struct mems mems_after_assign[7] = {{ 0 }};
	struct mems mems_margin[7] = {{ 0 }};

	/* All */
	ret = mems_reserved(&mems_after_assign[3]);
	INTERR(ret, "mems_reserved returned %d\n", ret);

	/* All - 1 */
	ret = mems_reserved(&mems_after_assign[5]);
	INTERR(ret, "mems_reserved returned %d\n", ret);
	ret = mems_pop(&mems_after_assign[5], 1);
	INTERR(ret, "mems_pop returned %d\n", ret);

	for (i = 0; i < 7; i++) {
		ret = mems_copy(&mems_margin[i], &mems_after_assign[i]);
		INTERR(ret, "mems_copy returned %d\n", ret);

		mems_fill(&mems_margin[i], 4UL << 20);
	}

	int ret_expected[] = {
		  -EINVAL,
		  -EINVAL,
		  0,
		  0,
		  -ENOMEM,
		  0,
		  -EINVAL,
		};

	struct mems *mems_expected[] = {
		  &mems_after_assign[0],
		  &mems_after_assign[1],
		  &mems_after_assign[2],
		  &mems_after_assign[3],
		  &mems_after_assign[4],
		  &mems_after_assign[5],
		  &mems_after_assign[6],
		};

	/* Activate and check */
	for (i = 0; i < 7; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_os_assign_mem(0, mems_input[i].mem_chunks,
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
