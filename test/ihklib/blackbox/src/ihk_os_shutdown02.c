#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "mem.h"
#include "params.h"
#include "linux.h"

const char param[] = "mem_chunks";
const char *values[] = {
	 "NULL",
	 "assigned",
	 "assigned + 1",
	 "assigned - 1",
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

	struct mems mems_input[4] = {
		{ .num_mem_chunks = 1, .mem_chunks = NULL },
		{ 0 },
		{ 0 },
		{ 0 },
	};

	for (i = 1; i < 4; i++) {
		ret = mems_reserved(&mems_input[i]);
		INTERR(ret, "mems_reserved returned %d\n", ret);
	}

	/* Plus one */
	ret = mems_push(&mems_input[2],
			mems_input[2].mem_chunks[0].size,
			mems_input[2].mem_chunks[0].numa_node_number);
	INTERR(ret, "mems_push returned %d\n", ret);

	/* Minus one */
	ret = mems_pop(&mems_input[3], 1);
	INTERR(ret, "mems_pop returned %d\n", ret);

	int ret_expected[] = {
		  -EFAULT,
		  0,
		  -EINVAL,
		  0,
	};

	struct mems mems_after_release[4] = {{ 0 }};
	struct mems mems_margin[4] = {{ 0 }};

	/* all */
	for (i = 0; i < 4; i++) {
		ret = mems_reserved(&mems_after_release[i]);
		INTERR(ret, "mems_reserved returned %d\n", ret);
	}

	/* Empty */
	ret = mems_shift(&mems_after_release[1],
			 mems_after_release[1].num_mem_chunks);
	INTERR(ret, "mems_shift returned %d\n", ret);

	/* Last one */
	ret = mems_shift(&mems_after_release[3],
			 mems_after_release[3].num_mem_chunks - 1);
	INTERR(ret, "mems_shift returned %d\n", ret);

	for (i = 0; i < 4; i++) {
		ret = mems_copy(&mems_margin[i], &mems_after_release[i]);
		INTERR(ret, "mems_copy returned %d\n", ret);

		mems_fill(&mems_margin[i], 4UL << 20);
	}

	struct mems *mems_expected[] = {
		&mems_after_release[0],
		&mems_after_release[1],
		&mems_after_release[2],
		&mems_after_release[3],
	};

	/* Activate and check */
	for (i = 0; i < 4; i++) {
		START("test-case: : %s\n", values[i]);

		ret = mems_os_assign();
		INTERR(ret, "mems_os_assign returned %d\n", ret);

		ret = ihk_os_release_mem(0, mems_input[i].mem_chunks,
				      mems_input[i].num_mem_chunks);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		ret = mems_check_assigned(mems_expected[i],
					  &mems_margin[i]);
		OKNG(ret == 0, "released as expected\n");

		ret = mems_os_release();
		INTERR(ret, "mems_os_release returned %d\n", ret);
	}

	ret = 0;
 out:
	linux_rmmod(0);
	return ret;
}

