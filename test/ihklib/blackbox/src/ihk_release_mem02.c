#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "mem.h"
#include "params.h"
#include "linux.h"

const char param[] = "mems";
const char *values[] = {
	"NULL",
	"reserved",
	"reserved + 1",
	"reserved - 1",
};

int main(int argc, char **argv)
{
	int ret;
	int i;

	params_getopt(argc, argv);

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	struct mems mems_input[4] = {
		{ .num_mem_chunks = 1, .mem_chunks = NULL },
		{ 0 },
		{ 0 },
		{ 0 },
	};

	struct mems mems_after_release[4] = { 0 };

	int ret_expected[] = {
		-EFAULT,
		0,
		-EINVAL,
		0,
	};

	struct mems *mems_expected[] = {
		&mems_after_release[0],
		&mems_after_release[1],
		&mems_after_release[2],
		&mems_after_release[3],
	};

	ret = mems_release();
	INTERR(ret, "mems_release returned %d\n", ret);

	/* Activate and check */
	for (i = 0; i < 4; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		ret = mems_reserve();
		INTERR(ret, "mems_reserve returned %d\n", ret);

		if (i != 0) {
			ret = mems_reserved(&mems_input[i]);
			INTERR(ret, "mems_reserved returned %d\n", ret);
		}

		/* Plus one */
		if (i == 2) {
			ret = mems_push(&mems_input[i],
				mems_input[i].mem_chunks[0].size,
				mems_input[i].mem_chunks[0].numa_node_number);
			INTERR(ret, "mems_push returned %d\n", ret);
		}
		/* Minus one */
		if (i == 3) {
			ret = mems_pop(&mems_input[i], 1);
			INTERR(ret, "mems_pop returned %d\n", ret);
		}

		ret = mems_reserved(&mems_after_release[i]);
		INTERR(ret, "mems_reserved returned %d\n", ret);

		/* Empty */
		if (i == 1 || i == 2) {
			ret = mems_shift(&mems_after_release[i],
					mems_after_release[i].num_mem_chunks);
			INTERR(ret, "mems_shift returned %d\n", ret);
		}
		/* Last one */
		if (i == 3) {
			ret = mems_shift(&mems_after_release[i],
					mems_after_release[i].num_mem_chunks -
					1);
			INTERR(ret, "mems_shift returned %d\n", ret);
		}

		ret = ihk_release_mem(0, mems_input[i].mem_chunks,
			mems_input[i].num_mem_chunks);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		ret = mems_check_reserved(mems_expected[i], NULL);
		OKNG(ret == 0, "released as expected\n");

		ret = mems_release();
		INTERR(ret, "ihk_release_mem returned %d\n", ret);
	}

	ret = 0;
 out:
	linux_rmmod(0);
	return ret;
}

