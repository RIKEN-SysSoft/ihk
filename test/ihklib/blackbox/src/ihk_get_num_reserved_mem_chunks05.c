#include <limits.h>
#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "mem.h"
#include "params.h"
#include "linux.h"

const char param[] = "user privilege";
const char *values[] = {
	 "root",
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

	struct mems mems_after_reserve[1] = { 0 };

	ret = mems_reserved(&mems_after_reserve[0]);
	INTERR(ret, "mems_reserved returned %d\n", ret);

	int ret_expected[1] = {
		mems_after_reserve[0].num_mem_chunks
	};

	/* Activate and check */
	for (i = 0; i < 1; i++) {
		ret = ihk_get_num_reserved_mem_chunks(0);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);
	}

	ret = 0;
 out:
	mems_release();
	linux_rmmod(0);
	return ret;
}
