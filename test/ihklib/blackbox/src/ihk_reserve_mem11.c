#include <errno.h>
#include <stdlib.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "mem.h"
#include "params.h"
#include "linux.h"

const char param[] =
	"all with different "
	"IHK_RESERVE_MEM_TIMEOUT values";
const char *values[] = {
	"180 sec",
	"0 sec (only 4 MiB chunks are searched)",
};

int main(int argc, char **argv)
{
	int ret;
	int i;

	params_getopt(argc, argv);

	/* Prepare one with NULL and zero-clear others */

	struct mems mems_input[2] = {{ 0 }};

	for (i = 0; i < 2; i++) {
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

	int mem_conf_keys[2] = {
		IHK_RESERVE_MEM_TIMEOUT,
		IHK_RESERVE_MEM_TIMEOUT,
	};

	int mem_conf_values[2] = { 180, 0 };

	int ret_expected[2] = { 0, 0 };
	struct mems mems_free_on_reserve[2] = {{ 0 }};

	struct mems mems_ratio[2] = {{ 0 }};
	unsigned long mems_ratio_expected[2] = { 98, 95 };

	double ratios[2][MAX_NUM_MEM_CHUNKS] = {{ 0 }};

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	/* Parse additional options */
	int opt;

	/* Don't request over 0.95 * NR_FREE_PAGES */
	int mem_conf_value = 95;

	while ((opt = getopt(argc, argv, "s")) != -1) {
		switch (opt) {
		case 's':
			/* no dedicated NUMA nodes for Linux */
			ret = ihk_reserve_mem_conf(0, IHK_RESERVE_MEM_MAX_SIZE_RATIO_ALL,
						   &mem_conf_value);
			INTERR(ret, "ihk_reserve_mem_conf returned %d\n",
			       ret);
			break;
		default: /* '?' */
			printf("unknown option %c\n", optopt);
			exit(1);
		}
	}

	/* Warm up */
	for (i = 0; i < 2; i++) {
		ret = ihk_reserve_mem_conf(0, mem_conf_keys[i],
					   &mem_conf_values[i]);
		INTERR(ret, "ihk_reserve_mem_conf returned %d\n",
		       ret);

		ret = ihk_reserve_mem(0, mems_input[i].mem_chunks,
				      mems_input[i].num_mem_chunks);
		INTERR(ret, "ihk_reserve_mem returned %d\n",
		       ret);

		ret = mems_release();
		INTERR(ret, "mems_release returned %d\n", ret);
	}

	/* Activate and check */
	for (i = 0; i < 2; i++) {
		int excess;

		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_reserve_mem_conf(0, mem_conf_keys[i],
					   &mem_conf_values[i]);
		INTERR(ret, "ihk_reserve_mem_conf returned %d\n",
		       ret);

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

		/* Check memory size measured as the ratio of free memory */
		ret = mems_copy(&mems_ratio[i],
				&mems_free_on_reserve[i]);
		INTERR(ret, "mems_copy returned %d\n", ret);

		mems_fill(&mems_ratio[i], mems_ratio_expected[i]);

		ret = mems_check_ratio(&mems_free_on_reserve[i],
				       &mems_ratio[i], ratios[i]);
		if (i == 0) {
			OKNG(ret == 0, "ratio of reserved to NR_FREE_PAGES\n");
		}

		if (i == 1) {
			int j;
			int fail = 0;

			for (j = 0; j < MAX_NUM_MEM_CHUNKS; j++) {
				if (ratios[i - 1][j] > 0 &&
				    ratios[i][j] > ratios[i - 1][j]) {
					fail = 1;
				}
			}
			OKNG(fail == 0, "less memory reserved with"
			     " smaller TIMEOUT\n");
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

