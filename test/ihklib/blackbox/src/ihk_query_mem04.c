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
	"1",
	"# of reserved",
	"# of reserved - 1",
	"# of reserved + 1",
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

	struct mems mems_input_reserve[8] = {{ 0 }};

	for (i = 0; i < 8; i++) {
		int excess;

		ret = mems_ls(&mems_input_reserve[i]);
		INTERR(ret, "mems_ls returned %d\n", ret);

		excess = mems_input_reserve[i].num_mem_chunks - 4;
		if (excess > 0) {
			ret = mems_shift(&mems_input_reserve[i], excess);
			INTERR(ret, "mems_ls returned %d\n", ret);
		}
	}

	struct mems mems_input[8] = {{ 0 }};

	int ret_expected[8] = {
		 -EINVAL,
		 -EINVAL,
		 -EINVAL,
		 -EINVAL,
		 0,
		 -EINVAL,
		 -EINVAL,
		 -EINVAL,
	};

	struct mems *mems_expected[8] = {
		  NULL, /* don't care */
		  NULL, /* don't care */
		  NULL, /* don't care */
		  NULL, /* don't care */
		  &mems_input_reserve[4],
		  NULL, /* don't care */
		  NULL, /* don't care */
		  NULL, /* don't care */
	};

	struct mems mems_margin[8] = {{ 0 }};

	ret = mems_copy(&mems_margin[4], &mems_input_reserve[4]);
	INTERR(ret, "mems_copy returned %d\n", ret);

	mems_fill(&mems_margin[4], 4UL << 20);

	/* Activate and check */
	for (i = 0; i < 8; i++) {
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

		switch (i) {
		case 0:
			mems_input[i].num_mem_chunks = INT_MIN;
			break;
		case 1:
			mems_input[i].num_mem_chunks = -1;
			break;
		case 2:
			mems_input[i].num_mem_chunks = 0;
			break;
		case 3:
			mems_input[i].num_mem_chunks = 1;
			break;
		case 4:
			mems_input[i].num_mem_chunks = num_mem_chunks;
			break;
		case 5:
			mems_input[i].num_mem_chunks = num_mem_chunks - 1;
			break;
		case 6:
			mems_input[i].num_mem_chunks = num_mem_chunks + 1;
			break;
		case 7:
			mems_input[i].num_mem_chunks = INT_MAX;
			break;
		default:
			break;
		}

		ret = ihk_query_mem(0, mems_input[i].mem_chunks,
				    mems_input[i].num_mem_chunks);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		if (mems_expected[i]) {
			ret = mems_compare(&mems_input[i],
					   mems_expected[i],
					   &mems_margin[i]);
			OKNG(ret == 0, "query result matches reserved\n");
		}

		/* Clean up */
		ret = mems_release();
		INTERR(ret, "ihk_release_mem returned %d\n", ret);
	}

	ret = 0;
 out:
	mems_release();
	linux_rmmod(0);
	return ret;
}
