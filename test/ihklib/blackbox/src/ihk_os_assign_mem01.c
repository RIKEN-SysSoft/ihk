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

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	ret = mems_reserve();
	INTERR(ret, "mems_reserve returned %d\n", ret);

	struct mems mems_input[2] = { 0 };

	for (i = 1; i < 2; i++) {
		ret = mems_reserved(&mems_input[i]);
		INTERR(ret, "mems_reserved returned %d\n", ret);
	}

	int ret_expected[] = { -ENOENT, 0 };

	struct mems mems_after_assign[2] = { 0 };
	struct mems mems_margin[2] = { 0 };

	for (i = 1; i < 2; i++) {
		ret = mems_reserved(&mems_after_assign[i]);
		INTERR(ret, "mems_reserved returned %d\n", ret);

		ret = mems_copy(&mems_margin[i], &mems_after_assign[i]);
		INTERR(ret, "mems_copy returned %d\n", ret);

		mems_fill(&mems_margin[i], 4UL << 20);
	}

	struct mems *mems_expected[] = { NULL, &mems_after_assign[1] };

	/* Activate and check */
	for (i = 0; i < 2; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_os_assign_mem(0, mems_input[i].mem_chunks,
				      mems_input[i].num_mem_chunks);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		if (mems_expected[i]) {
			ret = mems_check_assigned(mems_expected[i],
						  &mems_margin[i]);
			OKNG(ret == 0, "assigned as expected\n");

			/* Clean up */
			ret = mems_os_release();
			INTERR(ret, "mems_os_release returned %d\n", ret);
		}

		/* Precondition */
		if (i == 0) {
			ret = ihk_create_os(0);
			INTERR(ret, "ihk_create_os returned %d\n", ret);
		}
	}

	ret = 0;
 out:
	mems_release();
	linux_rmmod(0);

	return ret;
}
