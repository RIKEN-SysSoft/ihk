#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "mem.h"
#include "params.h"
#include "linux.h"

int main(int argc, char **argv)
{
	int ret;
	int i;

	params_getopt(argc, argv);

	const char param[] = "exsitence of IHK device file";
	const char *values[] = {
		 "without IHK device file",
		 "with IHK device file",
		};

	struct mems mems_input[2] = { 0 };
	for (i = 0; i < 2; i++) {
		int excess;

		ret = mems_ls(&mems_input[i], "MemFree", 0.9);
		INTERR(ret, "mems_ls returned %d\n", ret);

		excess = mems_input[i].num_mem_chunks - 4;
		if (excess > 0) {
			ret = mems_shift(&mems_input[i], excess);
			INTERR(ret, "mems_ls returned %d\n", ret);
		}
	}

	int ret_expected[] = { -ENOENT, 0 };

	struct mems mems_margin[2] = { 0 };

	for (i = 0; i < 2; i++) {
		int excess;

		ret = mems_ls(&mems_margin[i], "MemFree", 0.9);
		INTERR(ret, "mems_ls returned %d\n", ret);

		excess = mems_margin[i].num_mem_chunks - 4;
		if (excess > 0) {
			ret = mems_shift(&mems_margin[i], excess);
			INTERR(ret, "mems_shift returned %d\n", ret);
		}

		mems_fill(&mems_margin[i], 4UL << 20);
	}

	struct mems *mems_expected[] = { NULL, &mems_input[1] };

	/* Activate and check */
	for (i = 0; i < 2; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		/* Precondition */
		if (i == 1) {
			ret = linux_insmod(0);
			INTERR(ret, "linux_insmod returned %d\n", ret);
		}

		ret = ihk_reserve_mem(0, mems_input[i].mem_chunks,
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
