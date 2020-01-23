#include <limits.h>
#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "mem.h"
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

	struct mems mems_input_reserve[1] = { 0 };

	for (i = 0; i < 1; i++) {
		int excess;

		ret = mems_ls(&mems_input_reserve[i], "MemFree", 0.9);
		INTERR(ret, "mems_ls returned %d\n", ret);

		excess = mems_input_reserve[i].num_mem_chunks - 4;
		if (excess > 0) {
			ret = mems_shift(&mems_input_reserve[i], excess);
			INTERR(ret, "mems_ls returned %d\n", ret);
		}
	}

	struct mems mems_input[1] = { 0 };
	int ret_expected[1] = { 0 };

	struct mems *mems_expected[1] = {
		 &mems_input_reserve[0],
	};

	/* Activate and check */
	for (i = 0; i < 1; i++) {
		int num_mem_chunks;

		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_reserve_mem(0, mems_input_reserve[i].mem_chunks,
				      mems_input_reserve[i].num_mem_chunks);
		INTERR(ret, "ihk_reserve_mem returned %d\n", ret);

		ret = ihk_get_num_reserved_mem_chunks(0);
		INTERR(ret < 0, "ihk_get_num_reserved_mems returned %d\n", ret);
		num_mem_chunks = ret;

		ret = mems_init(&mems_input[i], num_mem_chunks);
		INTERR(ret, "mems_init returned %d\n", ret);

		ret = ihk_query_mem(0, mems_input[i].mem_chunks,
				    mems_input[i].num_mem_chunks);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		if (mems_expected[i]) {
			ret = mems_compare(&mems_input[i],
					mems_expected[i], NULL);
			OKNG(ret == 0, "query result matches reserved\n");

			/* Clean up */
			ret = mems_release();
			INTERR(ret, "ihk_release_mem returned %d\n", ret);
		}
	}

	ret = 0;
 out:
	mems_release();
	linux_rmmod(0);
	return ret;
}
