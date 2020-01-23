#include <limits.h>
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

	const char *messages[] = {
		 "INT_MIN",
		 "-1",
		 "0",
		 "1",
		 "# of reserved",
		 "# of reserved + 1",
		 "# of reserved - 1",
		 "INT_MAX",
		};

	struct mems mems_input_reserve_cpu[8] = { 0 };

	/* Both Linux and McKernel mem_chunks */
	for (i = 0; i < 8; i++) {
		ret = mem_chunks_ls(&mems_input_reserve_cpu[i]);
		INTERR(ret, "mem_chunks_ls returned %d\n", ret);

		/* Spare two mem_chunks for Linux */
		ret = mems_shift(&mems_input_reserve_cpu[i], 2);
		INTERR(ret, "mems_shift returned %d\n", ret);
	}

	struct mems mems_input[] = {
		 { .num_mem_chunks = INT_MIN, .mem_chunks = NULL },
		 { .num_mem_chunks = -1, .mem_chunks = NULL },
		 { .num_mem_chunks = 0, .mem_chunks = NULL },
		 { 0 },
		 { 0 },
		 { 0 },
		 { 0 },
		 { .num_mem_chunks = INT_MAX, .mem_chunks = NULL },
		};

	/* All of McKernel CPUs */
	for (i = 3; i < 7; i++) {
		ret = mem_chunks_ls(&mems_input[i]);
		INTERR(ret, "mem_chunks_ls returned %d\n", ret);
	}

	ret = mems_push(&mems_input[5], mem_chunks_input[5].num_mem_chunks);
	INTERR(ret, "mems_push returned %d\n", ret);

	ret = mems_pop(&mems_input[6], 1);
	INTERR(ret, "mems_pop returned %d\n", ret);

	for (i = 3; i < 7; i++) {
		/* Spare two mem_chunks for Linux */
		ret = mems_shift(&mems_input[i], 2);
		INTERR(ret, "mems_shift returned %d\n", ret);
	}

	/* First one */
	ret = mems_pop(&mems_input[3], mem_chunks_input[3].num_mem_chunks - 1);
	INTERR(ret, "mems_shift returned %d\n", ret);

	struct mems mems_after_release[8] = { 0 };

	/* All of McKernel CPUs */
	for (i = 0; i < 8; i++) {
		ret = mem_chunks_ls(&mems_after_release[i]);
		INTERR(ret, "mem_chunks_ls returned %d\n", ret);
	}

	for (i = 0; i < 8; i++) {
		/* Spare two mem_chunks for Linux */
		ret = mems_shift(&mems_after_release[i], 2);
		INTERR(ret, "mems_shift returned %d\n", ret);
	}

	/* Without first one */
	ret = mems_shift(&mems_after_release[3], 1);
	INTERR(ret, "mems_shift returned %d\n", ret);

	/* Empty */
	ret = mems_shift(&mems_after_release[4],
			 mems_after_release[4].num_mem_chunks);
	INTERR(ret, "mems_shift returned %d\n", ret);

	/* Last one */
	ret = mems_shift(&mems_after_release[6],
			 mems_after_release[6].num_mem_chunks - 1);
	INTERR(ret, "mems_shift returned %d\n", ret);

	int ret_expected_reserve_cpu[8] = { 0 };
	int ret_expected[] = {
		 -EINVAL,
		 -EINVAL,
		 0,
		 0,
		 0,
		 -EINVAL,
		 0,
		 -EINVAL,
		};

	struct mems *mems_expected[] = {
		  &mems_after_release[0],
		  &mems_after_release[1],
		  &mems_after_release[2],
		  &mems_after_release[3],
		  &mems_after_release[4],
		  &mems_after_release[5],
		  &mems_after_release[6],
		  &mems_after_release[7],
		};

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	/* Activate and check */
	for (i = 0; i < 8; i++) {
		START("test-case: num_mem_chunks: %s\n", messages[i]);

		ret = ihk_reserve_cpu(0, mems_input_reserve_cpu[i].mem_chunks,
				      mems_input_reserve_cpu[i].num_mem_chunks);
		INTERR(ret != ret_expected_reserve_cpu[i],
		     "ihk_reserve_cpu returned %d\n", ret);

		ret = ihk_release_cpu(0, mems_input[i].mem_chunks,
				    mems_input[i].num_mem_chunks);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		ret = mem_chunks_check_reserved(mems_expected[i]);
		OKNG(ret == 0, "released as expected\n");

		/* Clean up */
		ret = ihk_release_cpu(0, mems_after_release[i].mem_chunks,
				      mems_after_release[i].num_mem_chunks);
		INTERR(ret, "ihk_release_cpu returned %d\n", ret);
	}

	ret = 0;
 out:
	linux_rmmod(0);
	return ret;
}
