#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "mem.h"
#include "params.h"
#include "linux.h"

const char param[] = "existence of os instance";
const char *values[] = {
	"before ihk_create_os()",
	"after ihk_create_os()",
};

int main(int argc, char **argv)
{
	int ret;
	int i;

	params_getopt(argc, argv);

	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	ret = mems_reserve();
	INTERR(ret, "mems_reserve returned %d\n", ret);

	struct mems mems_input[2] = {{ 0 }};
	struct mems mems_input_assign_mem[2] = {{ 0 }};
	struct mems mems_after_release[2] = {{ 0 }};
	struct mems mems_margin[2] = {{ 0 }};

	for (i = 0; i < 2; i++) {
		ret = mems_reserved(&mems_input[i]);
		INTERR(ret, "mems_reserved returned %d\n", ret);

		ret = mems_reserved(&mems_input_assign_mem[i]);
		INTERR(ret, "mems_reserved returned %d\n", ret);

		ret = mems_reserved(&mems_after_release[i]);
		INTERR(ret, "mems_reserved returned %d\n", ret);

		ret = mems_copy(&mems_margin[i], &mems_after_release[i]);
		INTERR(ret, "mems_copy returned %d\n", ret);

		mems_fill(&mems_margin[i], 4UL << 20);
	}

	ret = mems_shift(&mems_after_release[1],
			mems_after_release[1].num_mem_chunks);
	INTERR(ret, "mems_shift returned %d\n", ret);

	int ret_expected_assign_mem[2] = { -ENOENT, 0 };
	int ret_expected[2] = { -ENOENT, 0 };

	struct mems *mems_expected[] = {
		NULL,
		&mems_after_release[1],
	};

	/* Activate and check */
	for (i = 0; i < 2; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_os_assign_mem(0, mems_input_assign_mem[i].mem_chunks,
				mems_input_assign_mem[i].num_mem_chunks);
		INTERR(ret != ret_expected_assign_mem[i],
		       "ihk_os_assign_mem returned %d\n", ret);

		ret = ihk_os_release_mem(0, mems_input[i].mem_chunks,
				      mems_input[i].num_mem_chunks);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		if (mems_expected[i]) {
			ret = mems_check_assigned(mems_expected[i],
						  &mems_margin[i]);
			OKNG(ret == 0, "reserved as expected\n");
		}

		/* Precondition */
		if (i == 0) {
			ret = ihk_create_os(0);
			INTERR(ret, "ihk_create_os returned %d\n", ret);
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
