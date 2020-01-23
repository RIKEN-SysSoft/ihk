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

	struct mems mems_after_reserve[2] = { 0 };

	int ret_expected[] = {
		-ENOENT,
		0, /* Filled in the loop */
	};

	/* Activate and check */
	for (i = 0; i < 2; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		/* Precondition */
		if (i == 1) {
			ret = linux_insmod(0);
			INTERR(ret, "linux_insmod returned %d\n", ret);

			ret = mems_reserve();
			INTERR(ret, "mems_reserve returned %d\n", ret);

			ret = mems_reserved(&mems_after_reserve[i]);
			INTERR(ret, "mems_reserved returned %d\n", ret);

			ret_expected[i] = mems_after_reserve[i].num_mem_chunks;
		}

		ret = ihk_get_num_reserved_mem_chunks(0);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		if (i == 1) {
			/* Clean up */
			ret = mems_release();
			INTERR(ret, "mems_release returned %d\n", ret);
		}
	}

	ret = 0;
 out:
	linux_rmmod(0);
	return ret;
}
