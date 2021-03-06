#include <errno.h>
#include <stdlib.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "mem.h"
#include "params.h"
#include "linux.h"

const char param[] = "memory chunk size";
const char *values[] = {
	"all (-1)",
};

int main(int argc, char **argv)
{
	int ret;
	int i;

	params_getopt(argc, argv);

	/* Prepare one with NULL and zero-clear others */

	struct mems mems_input[1] = {{ 0 }};

	for (i = 0; i < 1; i++) {
		int j;
		int excess;

		ret = _mems_ls(&mems_input[i], "MemTotal", 1.0, -1);
		INTERR(ret, "_mems_ls returned %d\n", ret);

		excess = mems_input[i].num_mem_chunks - 4;
		if (excess > 0) {
			ret = mems_shift(&mems_input[i], excess);
			INTERR(ret, "mems_shift returned %d\n", ret);
		}

		/* All */
		for (j = 0; j < mems_input[i].num_mem_chunks; j++) {
			mems_input[i].mem_chunks[j].size = -1;
		}
	}

	int ret_expected[1] = { 0 };
	struct mems mems_free_on_reserve[1] = {{ 0 }};
	struct mems mems_ratio[1] = {{ 0 }};
	double ratios[1][MAX_NUM_MEM_CHUNKS] = {{ 0 }};
	int no_dedicated_numa = 0;

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	/* Parse additional options */
	int opt;


	while ((opt = getopt(argc, argv, "s")) != -1) {
		switch (opt) {
		case 's': {
			/* Don't ask machines without system-service NUMA nodes
			 * for memory >= 0.90 * NR_FREE_PAGES
			 */
			int mem_conf_value = 90;

			no_dedicated_numa = 1;
			ret = ihk_reserve_mem_conf(0, IHK_RESERVE_MEM_MAX_SIZE_RATIO_ALL,
						   &mem_conf_value);
			INTERR(ret, "ihk_reserve_mem_conf returned %d\n",
			       ret);
			break; }
		default: /* '?' */
			printf("unknown option %c\n", optopt);
			exit(1);
		}
	}

	/* Activate and check */
	for (i = 0; i < 1; i++) {
		int excess;

		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_reserve_mem(0, mems_input[i].mem_chunks,
				      mems_input[i].num_mem_chunks);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		/* Scan Linux kmsg */
		ret = mems_free(&mems_free_on_reserve[i]);
		INTERR(ret, "mems_free returned %d\n", ret);

		excess = mems_free_on_reserve[i].num_mem_chunks - 4;
		if (excess > 0) {
			ret = mems_shift(&mems_free_on_reserve[i], excess);
			INTERR(ret, "mems_shift returned %d\n", ret);
		}

		ret = mems_copy(&mems_ratio[i],
				&mems_free_on_reserve[i]);
		INTERR(ret, "mems_copy returned %d\n", ret);

		mems_fill(&mems_ratio[i],
			  no_dedicated_numa ? 90 : 98);

		ret = mems_check_ratio(&mems_free_on_reserve[i],
				       &mems_ratio[i], ratios[i]);
		OKNG(ret == 0, "ratio of reserved to NR_FREE_PAGES\n");

		if (!no_dedicated_numa) {
#define LOWER_BOUND 30782652416UL
			ret = mems_check_total(LOWER_BOUND);
			OKNG(ret == 0, "total amount reserved\n");
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

