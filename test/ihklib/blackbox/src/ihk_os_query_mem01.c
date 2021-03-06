#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "mem.h"
#include "params.h"
#include "linux.h"

const char param[] = "existence of os instance";
const char *values[] = {
	"with os instance",
	"without os instance",
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

	/* All of McKernel CPUs */
	struct mems mems_input[2] = {{ 0 }};
	struct mems mems_after_assign[2] = {{ 0 }};

	for (i = 0; i < 2; i++) {
		ret = mems_reserved(&mems_input[i]);
		INTERR(ret, "mems_reserved returned %d\n", ret);

		ret = mems_reserved(&mems_after_assign[i]);
		INTERR(ret, "mems_reserved returned %d\n", ret);
	}

	ret = mems_shift(&mems_after_assign[0],
			mems_after_assign[0].num_mem_chunks);
	INTERR(ret, "mems_shift returned %d\n", ret);

	int ret_expected[] = {
		-ENOENT,
		0,
	};

	struct mems *mems_expected[] = {
		NULL,
		&mems_after_assign[1],
	};

	/* Activate and check */
	for (i = 0; i < 2; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		if (i == 1) {
			ret = ihk_create_os(0);
			INTERR(ret, "ihk_create_os returned %d\n", ret);

			ret = mems_os_assign();
			INTERR(ret, "mems_os_assign returned %d\n", ret);
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

		if (i == 1) {
			ret = mems_os_release();
			INTERR(ret, "mems_os_release returned %d\n", ret);

			ret = ihk_destroy_os(0, 0);
			INTERR(ret, "ihk_destroy_os returned %d\n", ret);
		}
	}

	ret = 0;
 out:
	if (ihk_get_num_os_instances(0)) {
		mems_os_release();
		ihk_destroy_os(0, 0);
	}
	mems_release();
	linux_rmmod(0);
	return ret;
}
