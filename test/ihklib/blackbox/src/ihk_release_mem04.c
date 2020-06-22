#include <limits.h>
#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "mem.h"
#include "params.h"
#include "linux.h"

const char param[] = "Num of memory chunks";
const char *values[] = {
	"INT_MIN",
	"-1",
	"0",
	"1",
	"# of reserved",
	"# of reserved + 1",
	"# of reserved - 1",
	"INT_MAX",
};

int main(int argc, char **argv)
{
	int ret;
	int i;

	params_getopt(argc, argv);

	struct mems mems_input_reserve_mem[8] = {{ 0 }};

	for (i = 0; i < 8; i++) {
		int excess;

		ret = mems_ls(&mems_input_reserve_mem[i]);
		INTERR(ret, "mems_ls returned %d\n", ret);

		excess = mems_input_reserve_mem[i].num_mem_chunks - 4;
		if (excess > 0) {
			ret = mems_shift(&mems_input_reserve_mem[i], excess);
			INTERR(ret, "mems_shift returned %d\n", ret);
		}
	}

	int ret_expected_reserve_mem[8] = { 0 };

	struct mems mems_input[8] = {{ 0 }};

	int ret_expected[] = {
		 -EINVAL,
		 -EINVAL,
		 0,
		 0,
		 0,
		 -EINVAL,
		 0,
		 -EINVAL,
	};
	struct mems mems_after_release[8] = {{ 0 }};
	struct mems *mems_expected[] = {
		&mems_after_release[0],
		&mems_after_release[1],
		&mems_after_release[2],
		&mems_after_release[3],
		&mems_after_release[4],
		&mems_after_release[5],
		&mems_after_release[6],
		&mems_after_release[7],
	};

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	/* Activate and check */
	for (i = 0; i < 8; i++) {
		int num_mem_chunks;

		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_reserve_mem(0, mems_input_reserve_mem[i].mem_chunks,
				      mems_input_reserve_mem[i].num_mem_chunks);
		INTERR(ret != ret_expected_reserve_mem[i],
		     "ihk_reserve_mem returned %d\n", ret);

		ret = ihk_get_num_reserved_mem_chunks(0);
		INTERR(ret < 0,
		     "ihk_get_num_reserved_mems returned %d\n", ret);
		num_mem_chunks = ret;

		ret = mems_init(&mems_input[i], num_mem_chunks);
		INTERR(ret, "mems_init returned %d\n", ret);
		ret = ihk_query_mem(0, mems_input[i].mem_chunks,
				    mems_input[i].num_mem_chunks);
		INTERR(ret, "ihk_query_mem returned %d\n", ret);

		ret = mems_init(&mems_after_release[i], num_mem_chunks);
		INTERR(ret, "mems_init returned %d\n", ret);
		ret = ihk_query_mem(0, mems_after_release[i].mem_chunks,
				    mems_after_release[i].num_mem_chunks);
		INTERR(ret, "ihk_query_mem returned %d\n", ret);


		if (i == 0) {
			mems_input[i].num_mem_chunks = INT_MIN;
		}

		if (i == 1) {
			mems_input[i].num_mem_chunks = -1;
		}

		if (i == 2) {
			mems_input[i].num_mem_chunks = 0;
		}

		if (i == 3) { /* first chunk */
			ret = mems_pop(&mems_input[i],
				       mems_input[i].num_mem_chunks - 1);
			INTERR(ret, "mems_pop returned %d\n", ret);

			ret = mems_shift(&mems_after_release[i], 1);
			INTERR(ret, "mems_shift returned %d\n", ret);
		}

		if (i == 4) { /* reserved */
			ret = mems_shift(&mems_after_release[i],
				mems_after_release[i].num_mem_chunks);
			INTERR(ret, "mems_shift returned %d\n", ret);
		}

		if (i == 5) { /* plus one */
			ret = mems_push(&mems_input[i],
					mems_input[i].mem_chunks[mems_input[i].num_mem_chunks - 1].size,
					mems_input[i].mem_chunks[mems_input[i].num_mem_chunks - 1].numa_node_number);
			INTERR(ret, "mems_push returned %d\n", ret);
			ret = mems_shift(&mems_after_release[i],
				mems_after_release[i].num_mem_chunks);
			INTERR(ret, "mems_shift returned %d\n", ret);
		}

		if (i == 6) { /* minus one */
			ret = mems_pop(&mems_input[i], 1);
			INTERR(ret, "mems_pop returned %d\n", ret);

			ret = mems_shift(&mems_after_release[i],
					 mems_after_release[i].num_mem_chunks - 1);
			INTERR(ret, "mems_shift returned %d\n", ret);
		}

		if (i == 7) {
			mems_input[i].num_mem_chunks = INT_MAX;
		}

		ret = ihk_release_mem(0, mems_input[i].mem_chunks,
				      mems_input[i].num_mem_chunks);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		if (mems_expected[i]) {
			ret = mems_check_reserved(mems_expected[i], NULL);
			OKNG(ret == 0, "released as expected\n");
		}

		/* Clean up */
		ret = mems_release();
		INTERR(ret, "ihk_release_mem returned %d\n", ret);
	}

	ret = 0;
 out:
	linux_rmmod(0);
	return ret;
}
