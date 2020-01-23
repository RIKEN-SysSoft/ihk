#include <limits.h>
#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "mem.h"
#include "params.h"
#include "linux.h"

const char param[] = "num_mem_chunks";
const char *values[] = {
	"INT_MIN",
	"-1",
	"0",
	"1",
	"# of reserved",
	"# of reserved + 1",
	"# of reserved - 1",
	"INT_MAX",
};

int main(int argc, char **argv)
{
	int ret;
	int i;

	params_getopt(argc, argv);

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	ret = mems_reserve();
	INTERR(ret, "mems_reserve returned %d\n", ret);

	ret = ihk_create_os(0);
	INTERR(ret, "ihk_create_os returned %d\n", ret);

	struct mems mems_input[8] = { 0 };
	struct mems mems_after_release[8] = { 0 };

	for (i = 0; i < 8; i++) {
		ret = mems_reserved(&mems_input[i]);
		INTERR(ret, "mems_reserved returned %d\n", ret);

		ret = mems_reserved(&mems_after_release[i]);
		INTERR(ret, "mems_reserved returned %d\n", ret);

		switch (i) {
		case 0:
			mems_input[i].num_mem_chunks = INT_MIN;
			break;
		case 1:
			mems_input[i].num_mem_chunks = -1;
			break;
		case 2:
			ret = mems_shift(&mems_input[i],
					 mems_input[i].num_mem_chunks);
			INTERR(ret, "mems_shift returned %d\n", ret);
			break;
		case 3:
			/* first chunk */
			ret = mems_pop(&mems_input[i],
				       mems_input[i].num_mem_chunks - 1);
			INTERR(ret, "mems_pop returned %d\n", ret);

			ret = mems_shift(&mems_after_release[i], 1);
			INTERR(ret, "mems_shift returned %d\n", ret);
			break;
		case 4:
			/* reserved */
			ret = mems_shift(&mems_after_release[i],
					 mems_after_release[i].num_mem_chunks);
			INTERR(ret, "mems_shift returned %d\n", ret);
			break;
		case 5:
			/* plus one */
			ret = mems_push(&mems_input[i],
					mems_input[i].mem_chunks[mems_input[i].num_mem_chunks - 1].size,
					mems_input[i].mem_chunks[mems_input[i].num_mem_chunks - 1].numa_node_number);
			INTERR(ret, "mems_push returned %d\n", ret);

			ret = mems_shift(&mems_after_release[i],
					 mems_after_release[i].num_mem_chunks);
			INTERR(ret, "mems_shift returned %d\n", ret);
			break;
		case 6:
			/* minus one */
			ret = mems_pop(&mems_input[i], 1);
			INTERR(ret, "mems_pop returned %d\n", ret);

			ret = mems_shift(&mems_after_release[i],
					 mems_after_release[i].num_mem_chunks - 1);
			INTERR(ret, "mems_shift returned %d\n", ret);
			break;
		case 7:
			mems_input[i].num_mem_chunks = INT_MAX;
			break;
		default:
			break;
		}
	}

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


	/* Activate and check */
	for (i = 0; i < 8; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		ret = mems_os_assign();
		INTERR(ret, "mems_os_assign returned %d\n", ret);

		ret = ihk_os_release_mem(0, mems_input[i].mem_chunks,
				mems_input[i].num_mem_chunks);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		if (mems_expected[i]) {
			ret = mems_check_assigned(mems_expected[i]);
			OKNG(ret == 0, "released as expected\n");

			/* Clean up */
			ret = mems_os_release();
			INTERR(ret, "mems_os_release returned %d\n", ret);
		}
	}

	ret = 0;
 out:
	mems_release();
	linux_rmmod(0);
	return ret;
}
