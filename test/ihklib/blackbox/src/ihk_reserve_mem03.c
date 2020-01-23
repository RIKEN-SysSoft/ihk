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

	int dev_index_input[] = {
		INT_MIN,
		-1,
		0,
		1,
		INT_MAX
	};

	struct mems mems_input[5] = { 0 };
	struct mems mems_after_reserve[5] = { 0 };
	struct mems mems_margin[5] = { 0 };

	/* All of McKernel CPUs */
	for (i = 0; i < 5; i++) {
		int excess;

		ret = mems_ls(&mems_input[i], "MemFree", 0.9);
		INTERR(ret, "mems_ls returned %d\n", ret);

		excess = mems_input[i].num_mem_chunks - 4;
		if (excess > 0) {
			ret = mems_shift(&mems_input[i], excess);
			INTERR(ret, "mems_ls returned %d\n", ret);
		}
	}

	ret = mems_copy(&mems_after_reserve[2], &mems_input[2]);
	INTERR(ret, "mems_copy returned %d\n", ret);

	ret = mems_copy(&mems_margin[2], &mems_input[2]);
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
		&mems_after_reserve[0],
		&mems_after_reserve[1],
		&mems_after_reserve[2],
		&mems_after_reserve[3],
		&mems_after_reserve[4],
	};

	/* Activate and check */
	for (i = 0; i < 5; i++) {
		START("test-case: dev_index: %s\n", values[i]);

		ret = ihk_reserve_mem(dev_index_input[i],
				      mems_input[i].mem_chunks,
				      mems_input[i].num_mem_chunks);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		if (mems_expected[i]) {
			ret = mems_check_reserved(mems_expected[i],
						  &mems_margin[i]);
			OKNG(ret == 0, "reserved as expected\n");

			/* Clean up */
			ret = mems_release();
			INTERR(ret, "mems_release returned %d\n", ret);
		}
	}

	ret = 0;
 out:
	linux_rmmod(0);
	return ret;
}
