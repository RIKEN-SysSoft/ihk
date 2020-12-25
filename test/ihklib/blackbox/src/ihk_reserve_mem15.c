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
	"best-effort balanced capped with 90% available "
	"where only 80% is available";

int main(int argc, char **argv)
{
	int ret;
	int i;
	int excess;

	struct mems mems_input[1] = { { 0 } };

	ret = _mems_ls(&mems_input[0], "MemFree", 0.9, -1);
	INTERR(ret, "_mems_ls returned %d\n", ret);

	excess = mems_input[0].num_mem_chunks - 4;
	if (excess > 0) {
		ret = mems_shift(&mems_input[0], excess);
		INTERR(ret, "mems_shift returned %d\n", ret);
	}

	mems_dump(&mems_input[0]);

	int mem_conf_input[1] = { 80 };
	int ret_expected[1] = { 0 };
	struct mems mems_after_reserve[1] = { { 0 } };

	ret = _mems_ls(&mems_after_reserve[0], "MemFree", 0.8, -1);
	INTERR(ret, "_mems_ls returned %d\n", ret);

	excess = mems_after_reserve[0].num_mem_chunks - 4;
	if (excess > 0) {
		ret = mems_shift(&mems_after_reserve[0], excess);
		INTERR(ret, "mems_shift returned %d\n", ret);
	}

	struct mems mems_margin[1] = { { 0 } };

	ret = mems_copy(&mems_margin[0], &mems_after_reserve[0]);
	mems_fill(&mems_margin[0], 4UL << 20);


	/* Compare MemFree of zoneinfo */
	struct mems memfree_before_reserve[1] = { { 0 } };

	ret = _mems_ls(&memfree_before_reserve[0], "MemFree", 1.0, -1);
	INTERR(ret, "_mems_ls returned %d\n", ret);

	excess = memfree_before_reserve[0].num_mem_chunks - 4;
	if (excess > 0) {
		ret = mems_shift(&memfree_before_reserve[0], excess);
		INTERR(ret, "mems_shift returned %d\n", ret);
	}

	/* to check if after_release is in the range of
	 * (before_reserve - 4MB, before_reserve + 4MB)
	 */
	mems_subtract(&memfree_before_reserve[0], 4UL << 20);

	struct mems memfree_margin[1] = { { 0 } };

	ret = mems_copy(&memfree_margin[0], &memfree_before_reserve[0]);
	mems_fill(&mems_margin[0], 8L << 20);

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	int rval = 1;

	ret = ihk_reserve_mem_conf(0, IHK_RESERVE_MEM_BALANCED_ENABLE,
				   &rval);
	INTERR(ret, "ihk_reserve_mem_conf returned %d\n",
	       ret);

	ret = ihk_reserve_mem_conf(0, IHK_RESERVE_MEM_BALANCED_BEST_EFFORT,
				   &rval);
	INTERR(ret, "ihk_reserve_mem_conf returned %d\n",
	       ret);

	int allowed_var = 10;

	ret = ihk_reserve_mem_conf(0, IHK_RESERVE_MEM_BALANCED_VARIANCE_LIMIT,
				   &allowed_var);
	INTERR(ret, "ihk_reserve_mem_conf returned %d\n",
	       ret);

	unsigned long sum_expected = 0;

	for (i = 0; i < mems_after_reserve[0].num_mem_chunks; i++) {
		sum_expected += mems_after_reserve[0].mem_chunks[i].size;
	}

	/* Activate and check */
	for (i = 0; i < 1; i++) {
		START("test-case: %s\n", param);

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

		ret = mems_check_total(sum_expected);
		OKNG(ret == 0, "total amount reserved > %lu\n",
		     sum_expected);

		ret = mems_check_var(allowed_var / (double)100);
		OKNG(ret == 0,
		     "NUMA-node variation of reserved size\n");

		/* Clean up */
		ret = mems_release();
		INTERR(ret, "mems_release returned %d\n", ret);

		/* Check if pages are returned to Linux */
		struct mems memfree_after_release[1] = { { 0 } };

		ret = _mems_ls(&memfree_after_release[0], "MemFree", 1.0, -1);
		INTERR(ret, "_mems_ls returned %d\n", ret);

		excess = memfree_after_release[0].num_mem_chunks - 4;
		if (excess > 0) {
			ret = mems_shift(&memfree_after_release[0], excess);
			INTERR(ret, "mems_shift returned %d\n", ret);
		}

		ret = mems_compare(&memfree_after_release[1],
				   &memfree_before_reserve[1],
				   &memfree_margin[1]);
		OKNG(ret, "MemFree after release is in the range of "
		     "[before_reserve - 4MB, before_reserve + 4MB)\n");
	}

	ret = 0;
 out:
	linux_rmmod(0);
	return ret;
}

