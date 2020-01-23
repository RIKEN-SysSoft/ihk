#include <errno.h>
#include <ihklib.h>
#include <numaif.h>
#include <numa.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "util.h"
#include "okng.h"
#include "mem.h"
#include "params.h"
#include "linux.h"

const char param[] =
	"total with different "
	"IHK_RESERVE_MEM_MAX_SIZE_RATIO_ALL";
const char *values[] = {
	"100%",
	"80%",
};

int main(int argc, char **argv)
{
	int ret;
	int i;
	int excess;

	params_getopt(argc, argv);

	struct mems mems_input[2] = { 0 };

	for (i = 0; i < 2; i++) {
		ret = mems_ls(&mems_input[i], "MemFree", 0.9);
		INTERR(ret, "mems_ls returned %d\n", ret);

		excess = mems_input[i].num_mem_chunks - 4;
		if (excess > 0) {
			ret = mems_shift(&mems_input[i], excess);
			INTERR(ret, "mems_shift returned %d\n", ret);
		}

		mems_dump(&mems_input[i]);
	}

	int mem_conf_input[2] = { 100, 80 };
	int ret_expected[2] = { 0, -ENOMEM };
	struct mems mems_after_reserve[2] = { 0 };

	ret = mems_copy(&mems_after_reserve[0], &mems_input[0]);
	INTERR(ret, "mems_copy returned %d\n", ret);

	struct mems mems_margin[2] = { 0 };

	ret = mems_copy(&mems_margin[0], &mems_input[0]);
	mems_fill(&mems_margin[0], 4UL << 20);

	struct mems *mems_expected[] = {
		&mems_after_reserve[0],
		&mems_after_reserve[1]
	};

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	int allowed_var = 10;

	ret = ihk_reserve_mem_conf(0, IHK_RESERVE_MEM_TOTAL,
				   &allowed_var);
	INTERR(ret, "ihk_reserve_mem_conf returned %d\n",
	       ret);

	unsigned long sum_expected = 0;

	for (i = 0; i < mems_after_reserve[0].num_mem_chunks; i++) {
		sum_expected += mems_after_reserve[0].mem_chunks[i].size;
	}

	/* Activate and check */
	for (i = 0; i < 2; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_reserve_mem_conf(0,
					   IHK_RESERVE_MEM_MAX_SIZE_RATIO_ALL,
					   &mem_conf_input[i]);
		INTERR(ret, "ihk_reserve_mem_conf returned %d\n",
		       ret);

		ret = ihk_reserve_mem(0, mems_input[i].mem_chunks,
				      mems_input[i].num_mem_chunks);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		if (i == 0) {
			ret = mems_check_total(sum_expected);
			OKNG(ret == 0, "total amount reserved %lu\n",
				sum_expected);

			ret = mems_check_var(allowed_var / (double)100);
			OKNG(ret == 0,
			     "NUMA-node variation of reserved size\n");
		}

		if (i == 1) {
			ret = mems_check_reserved(mems_expected[i],
						  &mems_margin[i]);
			OKNG(ret == 0, "reserved as expected\n");
		}

		/* Clean up */
		ret = mems_release();
		INTERR(ret, "mems_release returned %d\n", ret);
	}

	ret = 0;
 out:
	linux_rmmod(0);
	return ret;
}

