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
	"all",
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

	struct mems mems_input[6] = { 0 };
	struct mems mems_after_reserve[6] = { 0 };
	struct mems mems_margin[6] = { 0 };

	/* Both Linux and McKernel cpus */
	for (i = 0; i < 6; i++) {
		int excess;

		ret = mems_ls(&mems_input[i], "MemFree", 0.9);
		INTERR(ret, "mems_ls returned %d\n", ret);

		excess = mems_input[i].num_mem_chunks - 4;
		if (excess > 0) {
			ret = mems_shift(&mems_input[i], excess);
			INTERR(ret, "mems_shift returned %d\n", ret);
		}

		switch (i) {
		case 0: /* INT_MIN */
			mems_input[i].num_mem_chunks = INT_MIN;
			break;
		case 1: /* -1 */
			mems_input[i].num_mem_chunks = -1;
			break;
		case 2: /* zero */
			ret = mems_shift(&mems_input[i],
				mems_input[i].num_mem_chunks);
			INTERR(ret, "mems_shift returned %d\n", ret);
			break;
		case 3: /* one */
			ret = mems_pop(&mems_input[i],
				mems_input[i].num_mem_chunks - 1);
			INTERR(ret, "mems_pop returned %d\n", ret);

			ret = mems_copy(&mems_after_reserve[i],
					&mems_input[i]);
			INTERR(ret, "mems_copy returned %d\n", ret);

			break;
		case 4:
			ret = mems_copy(&mems_after_reserve[i],
					&mems_input[i]);
			INTERR(ret, "mems_copy returned %d\n", ret);
			break;
		case 5:
			mems_input[i].num_mem_chunks = INT_MAX;
			break;
		default:
			break;
		}

		switch (i) {
		case 2:
		case 3:
		case 4:
			ret = mems_copy(&mems_margin[i], &mems_input[i]);
			INTERR(ret, "mems_copy returned %d\n", ret);
			mems_fill(&mems_margin[i], 4UL << 20);
			break;
		}
	}

	int ret_expected[] = {
		-EINVAL,
		-EINVAL,
		0,
		0,
		0,
		-EINVAL,
	};

	struct mems *mems_expected[] = {
		&mems_after_reserve[0],
		&mems_after_reserve[1],
		&mems_after_reserve[2],
		&mems_after_reserve[3],
		&mems_after_reserve[4],
		&mems_after_reserve[5],
	};


	/* Activate and check */
	for (i = 0; i < 6; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_reserve_mem(0, mems_input[i].mem_chunks,
			mems_input[i].num_mem_chunks);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		if (mems_expected[i]) {
			ret = mems_check_reserved(mems_expected[i],
						  &mems_margin[i]);
			OKNG(ret == 0, "reserved as expected\n");

			ret = mems_release();
			INTERR(ret, "mems_release returned %d\n", ret);
		}
	}

	ret = 0;
 out:
	linux_rmmod(0);
	return ret;
}
