#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "mem.h"
#include "params.h"
#include "linux.h"

const char *values[] = {
	"non-root",
};

int main(int argc, char **argv)
{
	int ret;
	int i;
	int previleged = 0;

	params_getopt(argc, argv);

	struct mems mems_input[1] = {{ 0 }};
	struct mems mems_after_assign[1] = {{ 0 }};
	struct mems *mems_expected[1] = {
		&mems_after_assign[0]
	};
	int ret_expected[] = { -EPERM };

	/* Parse additional options */
	int opt;

	while ((opt = getopt(argc, argv, "ir")) != -1) {
		switch (opt) {
		case 'i':
			previleged = 1;

			/* Precondition */
			ret = linux_insmod(0);
			INTERR(ret, "linux_insmod returned %d\n", ret);

			ret = mems_reserve();
			INTERR(ret, "mems_reserve returned %d\n", ret);

			ret = ihk_create_os(0);
			INTERR(ret, "ihk_create_os returned %d\n", ret);

			exit(0);
			break;
		case 'r':
			previleged = 1;

			/* Check there's no side effects */
			if (mems_expected[0]) {
				ret = mems_check_assigned(mems_expected[0],
							  NULL);
				OKNG(ret == 0, "assigned as expected\n");
			}

			/* Clean up */
			ret = ihk_destroy_os(0, 0);
			INTERR(ret, "ihk_destroy_os returned %d\n", ret);

			ret = mems_release();
			INTERR(ret, "mems_release returned %d\n", ret);

			ret = linux_rmmod(1);
			INTERR(ret, "rmmod returned %d\n", ret);
			exit(0);
			break;
		default: /* '?' */
			printf("unknown option %c\n", optopt);
			exit(1);
		}
	}


	/* Activate and check */
	for (i = 0; i < 1; i++) {
		START("test-case: user privilege: %s\n", values[i]);

		ret = mems_push(&mems_input[i], -1, 0);
		INTERR(ret, "mems_push returned %d\n", ret);

		ret = linux_wait_chmod(0);
		INTERR(ret, "mode didn't changed to 0666\n");

		ret = ihk_os_assign_mem(0, mems_input[i].mem_chunks,
				      mems_input[i].num_mem_chunks);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);
	}

	ret = 0;
 out:
	if (previleged) {
		if (ihk_get_num_os_instances(0)) {
			mems_os_release();
			ihk_destroy_os(0, 0);
		}
		mems_release();
		linux_rmmod(0);
	}
	return ret;
}
