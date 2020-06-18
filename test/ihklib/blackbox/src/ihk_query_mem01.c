#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "mem.h"
#include "params.h"
#include "linux.h"

const char param[] = "exsitence of IHK device file";
const char *values[] = {
	"without IHK device file",
	"with IHK device file",
};

int main(int argc, char **argv)
{
	int ret;
	int i;

	params_getopt(argc, argv);

	struct mems mems_input[2] = {{ 0 }};

	/* Reference for sum of the sizes */
	struct mems mems_input_reserve[2] = {{ 0 }};

	for (i = 0; i < 2; i++) {
		int excess;

		ret = mems_ls(&mems_input_reserve[i]);
		INTERR(ret, "mems_ls returned %d\n", ret);

		excess = mems_input_reserve[i].num_mem_chunks - 4;
		if (excess > 0) {
			ret = mems_shift(&mems_input_reserve[i], excess);
			INTERR(ret, "mems_ls returned %d\n", ret);
		}
	}

	/* Reference for # of memory chunks */
	struct mems mems_expected_num_mem_chunks[2] = {{ 0 }};
	struct mems *mems_expected[] = { NULL, &mems_input_reserve[1] };
	struct mems mems_margin[2] = {{ 0 }};

	ret = mems_copy(&mems_margin[1], &mems_input_reserve[1]);
	INTERR(ret, "mems_copy returned %d\n", ret);

	mems_fill(&mems_margin[1], 4UL << 20);

	int ret_expected_get_num[] = {
		-ENOENT,
		0 /* filled in the loop */
	};
	int ret_expected[] = { -ENOENT, 0 };

	/* Activate and check */
	for (i = 0; i < 2; i++) {
		int num_mem_chunks;

		START("test-case: %s: %s\n", param, values[i]);

		/* Precondition */
		if (i == 1) {
			ret = linux_insmod(0);
			INTERR(ret, "linux_insmod returned %d\n", ret);

			ret = ihk_reserve_mem(0,
					mems_input_reserve[i].mem_chunks,
					mems_input_reserve[i].num_mem_chunks);
			INTERR(ret, "ihk_reserve_mem returned %d\n", ret);

			ret = mems_reserved(&mems_expected_num_mem_chunks[i]);
			INTERR(ret, "mems_reserved returned %d\n", ret);
			ret_expected_get_num[i] =
				mems_expected_num_mem_chunks[1].num_mem_chunks;
		}

		ret = ihk_get_num_reserved_mem_chunks(0);
		INTERR(ret != ret_expected_get_num[i],
		       "ihk_get_num_reserved_mems returned %d\n", ret);
		num_mem_chunks = ret < 0 ? 0 : ret;

		ret = mems_init(&mems_input[i], num_mem_chunks);
		INTERR(ret, "mems_init returned %d\n", ret);

		ret = ihk_query_mem(0, mems_input[i].mem_chunks,
				mems_input[i].num_mem_chunks);
		OKNG(ret == ret_expected[i],
			"return value: %d, expected: %d\n",
			ret, ret_expected[i]);

		if (mems_expected[i]) {
			/* Note that only per-NUMA-node-sum is compared */
			ret = mems_compare(&mems_input[i], mems_expected[i],
					   &mems_margin[i]);
			OKNG(ret == 0, "query result matches reserved\n");
		}

		/* Clean up */
		if (i == 1) {
			ret = mems_release();
			INTERR(ret, "mems_release returned %d\n", ret);
		}
	}

	ret = 0;
 out:
	linux_rmmod(0);
	return ret;
}
