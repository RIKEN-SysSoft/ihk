#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "mem.h"
#include "params.h"
#include "linux.h"

const char param[] = "numa node";
const char *values[] = {
	"outside /sys/fs/cgroup/cpuset/pxkrmjobs.slice/cpuset.mems",
};

int main(int argc, char **argv)
{
	int ret;
	int i;

	/* Prepare one with NULL and zero-clear others */

	struct mems mems_input[1] = { { 0 } };

	for (i = 0; i < 1; i++) {
		int excess;

		ret = _mems_ls(&mems_input[i], "MemFree", 0.9, -1);
		INTERR(ret, "_mems_ls returned %d\n", ret);

		excess = mems_input[i].num_mem_chunks - 4;
		if (excess > 0) {
			ret = mems_shift(&mems_input[i], excess);
			INTERR(ret, "mems_shift returned %d\n", ret);
		}
	}

	/* expecting numa#3 */
	ret = mems_push(&mems_input[0],
			1ULL << 30,
			mems_min_id(&mems_input[0]) - 1);
	INTERR(ret, "mems_push returned %d\n", ret);

	int ret_expected[] = { -EINVAL };
	struct mems mems_after_reserve[] = { { 0 } };
	struct mems mems_margin[3] = { { 0 } };

	for (i = 0; i < 1; i++) {
		ret = mems_copy(&mems_margin[i], &mems_input[i]);
		INTERR(ret, "mems_copy failed with %d\n", ret);
		mems_fill(&mems_margin[i], 4UL << 20);
	}

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	/* Activate and check */
	for (i = 0; i < 1; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_reserve_mem(0, mems_input[i].mem_chunks,
				      mems_input[i].num_mem_chunks);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		ret = mems_check_reserved(&mems_after_reserve[i],
					  &mems_margin[i]);
		OKNG(ret == 0, "reserved as expected\n");

		/* Clean up */
		ret = mems_release();
		INTERR(ret, "mems_release returned %d\n", ret);
	}

	ret = 0;
 out:
	linux_rmmod(0);
	return ret;
}

