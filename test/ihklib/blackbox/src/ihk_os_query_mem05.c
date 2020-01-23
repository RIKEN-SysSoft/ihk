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
	"reserved",
	"reserved + 1",
	"reserved - 1",
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
	struct mems mems_after_assign[8] = { 0 };

	ret = mems_reserved(&mems_after_assign[4]);
	INTERR(ret, "mems_reserved returned %d\n", ret);

	int ret_expected[8] = {
		-EINVAL,
		-EINVAL,
		-EINVAL,
		-EINVAL,
		0,
		-EINVAL,
		-EINVAL,
		-EINVAL,
	};

	struct mems *mems_expected[8] = {
		NULL,
		NULL,
		NULL,
		NULL,
		&mems_after_assign[4],
		NULL,
		NULL,
		NULL,
	};

	/* Activate and check */
	for (i = 0; i < 8; i++) {
		int num_mem_chunks;

		START("test-case: %s: %s\n", param, values[i]);

		ret = mems_os_assign();
		INTERR(ret, "mems_os_assign returned %d\n", ret);

		ret = ihk_os_get_num_assigned_mem_chunks(0);
		INTERR(ret < 0,
			"ihk_os_get_num_assigned_mem_chunks returned %d\n",
			ret);
		num_mem_chunks = ret;

		ret = mems_init(&mems_input[i], num_mem_chunks);
		INTERR(ret, "mems_init returned %d\n", ret);

		switch (i) {
		case 0:
			mems_input[i].num_mem_chunks = INT_MIN;
			break;
		case 1:
			mems_input[i].num_mem_chunks = -1;
			break;
		case 2:
			mems_input[i].num_mem_chunks = 0;
			break;
		case 3:
			mems_input[i].num_mem_chunks = 1;
			break;
		case 5:
			mems_input[i].num_mem_chunks -= 1;
			break;
		case 6:
			mems_input[i].num_mem_chunks += 1;
			break;
		case 7:
			mems_input[i].num_mem_chunks = INT_MAX;
			break;
		default:
			break;
		}

		ret = ihk_os_query_mem(0, mems_input[i].mem_chunks,
			mems_input[i].num_mem_chunks);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		if (mems_expected[i]) {
			ret = mems_compare(&mems_input[i],
					   mems_expected[i], NULL);
			OKNG(ret == 0, "query result matches input\n");
		}
		/* Clean up */
		ret = mems_os_release();
		INTERR(ret, "mems_os_release returned %d\n", ret);
	}

	ret = 0;
 out:
	mems_release();
	linux_rmmod(0);
	return ret;
}

