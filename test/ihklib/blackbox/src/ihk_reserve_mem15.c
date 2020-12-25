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
	"capped best-effort balanced with "
	"partial releases of a large amount";

#define MARGIN (32UL << 20)

int main(int argc, char **argv)
{
	int ret;
	int i;
	int excess;
	int opt;
	size_t memsize = 2UL << 30;

	while ((opt = getopt(argc, argv, "s:")) != -1) {
		switch (opt) {
		case 's': /* user mode */
			memsize = strtol(optarg, NULL, 10) << 20;
			break;
		default:
			printf("unknown option %c\n", optopt);
			exit(1);
		}
	}

	/* MemFree of zoneinfo before reserve */
	struct mems memfree_before_reserve[1] = { { 0 } };

	ret = _mems_ls(&memfree_before_reserve[0], "MemFree", 1.0, -1);
	INTERR(ret, "_mems_ls returned %d\n", ret);

	excess = memfree_before_reserve[0].num_mem_chunks - 4;
	if (excess > 0) {
		ret = mems_shift(&memfree_before_reserve[0], excess);
		INTERR(ret, "mems_shift returned %d\n", ret);
	}


	/* to check if after_release is in the range of
	 * (before_reserve - margin, before_reserve + margin)
	 */
	mems_subtract(&memfree_before_reserve[0], MARGIN);

	struct mems memfree_margin[1] = { { 0 } };

	ret = mems_copy(&memfree_margin[0], &memfree_before_reserve[0]);
	INTERR(ret, "mems_copy returned %d\n", ret);
	mems_fill(&memfree_margin[0], MARGIN * 2);

	/* 2G@4-7 */
	struct mems mems_input[1] = { { 0 } };

	ret = mems_copy(&mems_input[0], &memfree_before_reserve[0]);
	INTERR(ret, "mems_copy returned %d\n", ret);
	mems_fill(&mems_input[0], memsize);

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


	int mem_conf_input = 95;

	ret = ihk_reserve_mem_conf(0,
				   IHK_RESERVE_MEM_MAX_SIZE_RATIO_ALL,
				   &mem_conf_input);
	INTERR(ret, "ihk_reserve_mem_conf returned %d\n",
	       ret);

	/* Activate and check */
	for (i = 0; i < 5; i++) {
		START("test-case: %s\n", param);

		ret = ihk_reserve_mem(0, mems_input[0].mem_chunks,
				      mems_input[0].num_mem_chunks);
		INTERR(ret, "ihk_reserve_mem failed with %d\n", ret);

		ret = mems_release();
		INTERR(ret, "mems_release failed with %d\n", ret);

		/* Check if all pages are returned to Linux */
		struct mems memfree_after_release[1] = { { 0 } };

		ret = _mems_ls(&memfree_after_release[0], "MemFree", 1.0, -1);
		INTERR(ret, "_mems_ls returned %d\n", ret);

		excess = memfree_after_release[0].num_mem_chunks - 4;
		if (excess > 0) {
			ret = mems_shift(&memfree_after_release[0], excess);
			INTERR(ret, "mems_shift returned %d\n", ret);
		}

		ret = mems_compare(&memfree_after_release[0],
				   &memfree_before_reserve[0],
				   &memfree_margin[0]);
		OKNG(ret == 0, "MemFree after release is in the range of "
		     "[before_reserve - 4MB, before_reserve + 4MB)\n");
	}

	ret = 0;
 out:
	linux_rmmod(0);
	return ret;
}

