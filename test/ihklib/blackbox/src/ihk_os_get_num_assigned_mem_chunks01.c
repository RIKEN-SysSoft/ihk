#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "mem.h"
#include "params.h"
#include "linux.h"

const char param[] = "exsitence of OS instance";
const char *values[] = {
	"without os instance",
	"with os instance",
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

	struct mems mems_after_assigned[2] = { 0 };

	for (i = 1; i < 2; i++) {
		ret = mems_reserved(&mems_after_assigned[i]);
		INTERR(ret, "mems_reserved returned %d\n", ret);
	}

	int ret_expected[] = {
		-ENOENT,
		mems_after_assigned[1].num_mem_chunks,
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

		ret = ihk_os_get_num_assigned_mem_chunks(0);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		if (i == 1) {
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
