#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "mem.h"
#include "params.h"
#include "linux.h"

const char param[] = "MemTotal";
const char *values[] = {
	"MemTotal * 0.9",
	"MemTotal * 1.0",
	"MemTotal * 1.1",
};

int main(int argc, char **argv)
{
	int ret;
	int i;

	params_getopt(argc, argv);

	/* Prepare one with NULL and zero-clear others */

	struct mems mems_input[3] = {{ 0 }};
	double ratios[3] = { 0.9, 1.0, 1.1 };

	for (i = 0; i < 3; i++) {
		int excess;

		ret = _mems_ls(&mems_input[i], "MemTotal", ratios[i], -1);
		INTERR(ret, "_mems_ls returned %d\n", ret);

		excess = mems_input[i].num_mem_chunks - 4;
		if (excess > 0) {
			ret = mems_shift(&mems_input[i], excess);
			INTERR(ret, "mems_shift returned %d\n", ret);
		}
	}

	int ret_expected[] = { 0, -ENOMEM, -ENOMEM };
	struct mems mems_after_reserve[3] = {{ 0 }};

	for (i = 0; i < 3; i++) {
		int excess;

		ret = _mems_ls(&mems_after_reserve[i], "MemTotal",
			       ratios[i], -1);
		INTERR(ret, "_mems_ls returned %d\n", ret);

		excess = mems_after_reserve[i].num_mem_chunks - 4;
		if (excess > 0) {
			ret = mems_shift(&mems_after_reserve[i], excess);
			INTERR(ret, "mems_shift returned %d\n", ret);
		}
	}

	/* Empty */
	for (i = 1; i < 3; i++) {
		ret = mems_shift(&mems_after_reserve[i],
				 mems_after_reserve[i].num_mem_chunks);
		INTERR(ret, "mems_shift returned %d\n", ret);
	}

	struct mems mems_margin[3] = {{ 0 }};

	for (i = 0; i < 3; i++) {
		int excess;

		ret = _mems_ls(&mems_margin[i], "MemTotal", 1.0, -1);
		INTERR(ret, "_mems_ls returned %d\n", ret);

		excess = mems_margin[i].num_mem_chunks - 4;
		if (excess > 0) {
			ret = mems_shift(&mems_margin[i], excess);
			INTERR(ret, "mems_shift returned %d\n", ret);
		}

		mems_fill(&mems_margin[i], 4UL << 20);
	}

	struct mems *mems_expected[3] = {
		&mems_after_reserve[0],
		&mems_after_reserve[1],
		&mems_after_reserve[2],
	};

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	/* Activate and check */
	for (i = 0; i < 3; i++) {
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

