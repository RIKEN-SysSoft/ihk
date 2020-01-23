#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "mem.h"
#include "params.h"
#include "linux.h"

const char param[] = "num_chunks";
const char *values[] = {
	"NULL",
	"MemFree * 0.9",
};

int main(int argc, char **argv)
{
	int ret;
	int i;

	params_getopt(argc, argv);

	/* Reference for sum of the sizes */
	struct mems mems_expected_size[2] = { 0 };

	for (i = 0; i < 2; i++) {
		int excess;

		ret = mems_ls(&mems_expected_size[i], "MemFree", 0.9);
		INTERR(ret, "mems_ls returned %d\n", ret);

		excess = mems_expected_size[i].num_mem_chunks - 4;
		if (excess > 0) {
			ret = mems_shift(&mems_expected_size[i], excess);
			INTERR(ret, "mems_ls returned %d\n", ret);
		}
	}

	/* Reference for # of memory chunks */
	struct mems mems_expected_num_mem_chunks[2] = { 0 };

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	int ret_expected[2] = { 0 };
	struct mems mems_query[2] = { 0 };
	struct mems *mems_expected[] = { NULL, &mems_expected_size[1] };

	/* Activate and check */
	for (i = 0; i < 2; i++) {
		int num_mem_chunks;

		START("test-case: %s: %s\n", param, values[i]);

		if (i == 1) {
			ret = ihk_reserve_mem(0, mems_expected_size[i].mem_chunks,
					      mems_expected_size[i].num_mem_chunks);
			INTERR(ret, "ihk_reserve_mem returned %d\n", ret);

			ret = mems_reserved(&mems_expected_num_mem_chunks[i]);
			INTERR(ret, "mems_reserved returned %d\n", ret);
			ret_expected[i] =
				mems_expected_num_mem_chunks[1].num_mem_chunks;
		}

		ret = ihk_get_num_reserved_mem_chunks(0);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);
		num_mem_chunks = ret;

		/* Check the total size to check # of mem chunks */
		if (mems_expected[i]) {
			ret = mems_init(&mems_query[i], num_mem_chunks);
			INTERR(ret, "mems_init returned %d\n", ret);

			ret = ihk_query_mem(0, mems_query[i].mem_chunks,
					    mems_query[i].num_mem_chunks);
			INTERR(ret, "ihk_query_mem returned %d\n", ret);

			ret = mems_compare(&mems_query[i], mems_expected[i],
					   NULL);
			OKNG(ret == 0,
			     "total size of query result matches reserved\n");
		}

		/* Clean up */
		if (i == 1) {
			ret = mems_release();
			INTERR(ret, "mems_reserve returned %d\n", ret);
		}
	}

	ret = 0;
 out:
	linux_rmmod(0);
	return ret;
}

