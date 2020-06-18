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

	int dev_index_input[] = {
		INT_MIN,
		-1,
		0,
		1,
		INT_MAX
	};

	struct mems mems_input[5] = {{ 0 }};

	for (i = 0; i < 5; i++) {
		int excess;

		ret = mems_ls(&mems_input[i]);
		INTERR(ret, "mems_ls returned %d\n", ret);

		excess = mems_input[i].num_mem_chunks - 4;
		if (excess > 0) {
			ret = mems_shift(&mems_input[i], excess);
			INTERR(ret, "mems_shift returned %d\n", ret);
		}
	}

	struct mems mems_after_release[5] = {{ 0 }};

	int ret_expected[] = {
		  -ENOENT,
		  -ENOENT,
		  0,
		  -ENOENT,
		  -ENOENT,
		};

	struct mems *mems_expected[] = {
		  &mems_after_release[0],
		  &mems_after_release[1],
		  &mems_after_release[2],
		  &mems_after_release[3],
		  &mems_after_release[4],
		};

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	/* Activate and check */
	for (i = 0; i < 5; i++) {
		int num_mem_chunks;

		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_reserve_mem(0, mems_input[i].mem_chunks,
				      mems_input[i].num_mem_chunks);
		INTERR(ret, "ihk_reserve_mem returned %d\n", ret);

		ret = ihk_get_num_reserved_mem_chunks(0);
		INTERR(ret < 0, "ihk_get_num_reserved_mem_chunks returned %d\n",
		       ret);
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

		if (i == 2) {
			ret = mems_shift(&mems_after_release[i],
				mems_after_release[i].num_mem_chunks);
			INTERR(ret, "mems_shift returned %d\n", ret);
		}

		ret = ihk_release_mem(dev_index_input[i],
				mems_input[i].mem_chunks,
				mems_input[i].num_mem_chunks);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		ret = mems_check_reserved(mems_expected[i], NULL);
		OKNG(ret == 0, "released as expected\n");

		/* Clean up */
		ret = mems_release();
		INTERR(ret, "ihk_release_mem returned %d\n", ret);
	}

	ret = 0;
 out:
	linux_rmmod(0);
	return ret;
}
