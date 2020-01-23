#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "mem.h"
#include "params.h"
#include "linux.h"

const char param[] = "exsitence of IHK device file";
const char *values[] = {
	"without IHK device file",
	"with IHK device file",
};

int main(int argc, char **argv)
{
	int ret;
	int i;

	params_getopt(argc, argv);

	struct mems mems_input_reserve_mem[2] = { 0 };

	for (i = 0; i < 2; i++) {
		int excess;

		ret = mems_ls(&mems_input_reserve_mem[i], "MemFree", 0.9);
		INTERR(ret, "mems_ls returned %d\n", ret);

		excess = mems_input_reserve_mem[i].num_mem_chunks - 4;
		if (excess > 0) {
			ret = mems_shift(&mems_input_reserve_mem[i], excess);
			INTERR(ret, "mems_shift returned %d\n", ret);
		}
	}

	struct mems mems_input[2] = { 0 };

	int ret_expected[] = { -ENOENT, 0 };
	int ret_expected_reserve_mem[] = { -ENOENT, 0 };

	struct mems mems_after_release[] = {
		{ 0 },
		{ .mem_chunks = NULL, .num_mem_chunks = 0 },
	};

	struct mems *mems_expected[] = {
		NULL,
		&mems_after_release[1],
	};

	/* Activate and check */
	for (i = 0; i < 2; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_reserve_mem(0, mems_input_reserve_mem[i].mem_chunks,
				      mems_input_reserve_mem[i].num_mem_chunks);
		INTERR(ret != ret_expected_reserve_mem[i],
		       "ihk_reserve_mem returned %d\n", ret);

		if (ret_expected_reserve_mem[i] == 0) {
			ret = ihk_get_num_reserved_mem_chunks(0);
			INTERR(ret < 0,
			       "ihk_get_num_reserved_mem_chunks returned %d\n",
			       ret);

			ret = mems_init(&mems_input[i], ret);
			INTERR(ret, "mems_init returned %d\n", ret);

			ret = ihk_query_mem(0, mems_input[i].mem_chunks,
					    mems_input[i].num_mem_chunks);
			INTERR(ret, "ihk_query_mem returned %d\n", ret);
		}

		ret = ihk_release_mem(0, mems_input[i].mem_chunks,
				      mems_input[i].num_mem_chunks);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		if (mems_expected[i]) {
			ret = mems_check_reserved(mems_expected[i], NULL);
			OKNG(ret == 0, "released as expected\n");
		}

		if (i == 0) {
			/* Precondition */
			ret = linux_insmod(0);
			INTERR(ret, "linux_insmod returned %d\n", ret);
		}
	}

	ret = 0;
 out:
	linux_rmmod(0);
	return ret;
}
