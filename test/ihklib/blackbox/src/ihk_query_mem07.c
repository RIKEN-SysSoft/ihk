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

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	struct mems mems_expected_size[2] = { 0 };

	for (i = 1; i < 2; i++) {
		int excess;

		ret = mems_ls(&mems_expected_size[i], "MemFree", 0.9); /* 90% */
		INTERR(ret, "mems_ls returned %d\n", ret);

		excess = mems_expected_size[i].num_mem_chunks - 4;
		if (excess > 0) {
			ret = mems_shift(&mems_expected_size[i], excess);
			INTERR(ret, "mems_ls returned %d\n", ret);
		}
	}

	int ret_expected[2] = { 0 };

	struct mems mems_input[2] = { 0 };
	struct mems *mems_expected[2] = {
		&mems_expected_size[0],
		&mems_expected_size[1]
	};

	/* Activate and check */
	for (i = 0; i < 2; i++) {
		int num_mem_chunks;

		START("test-case: %s: %s\n", param, values[i]);

		if (i == 1) {
			ret = ihk_reserve_mem(0, mems_expected_size[i].mem_chunks,
					      mems_expected_size[i].num_mem_chunks);
			INTERR(ret, "ihk_reserve_mem returned %d\n", ret);
		}

		ret = ihk_get_num_reserved_mem_chunks(0);
		INTERR(ret < 0,
		       "ihk_get_num_reserved_mem_chunks returned %d\n", ret);
		num_mem_chunks = ret;

		ret = mems_init(&mems_input[i], num_mem_chunks);
		INTERR(ret, "mems_init returned %d\n", ret);

		ret = ihk_query_mem(0, mems_input[i].mem_chunks,
				    mems_input[i].num_mem_chunks);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		/* Check the total size to check # of mem chunks */
		if (mems_expected[i]) {

			ret = mems_compare(&mems_input[i],
					mems_expected[i], NULL);
			OKNG(ret == 0,
			     "total size of query result matches reserved\n");
		}

		ret = mems_release();
		INTERR(ret, "mems_reserve returned %d\n", ret);
	}

	ret = 0;
 out:
	linux_rmmod(0);
	return ret;
}
