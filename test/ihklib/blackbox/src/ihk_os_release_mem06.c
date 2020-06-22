#include <stdlib.h>
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
	"non-root",
};

int main(int argc, char **argv)
{
	int ret;
	int i;
	int previleged = 0;

	params_getopt(argc, argv);

	/* Parse additional options */
	int opt;

	while ((opt = getopt(argc, argv, "ir")) != -1) {
		switch (opt) {
		case 'i': {
			previleged = 1;

			/* Precondition */
			ret = linux_insmod(0);
			INTERR(ret, "linux_insmod returned %d\n", ret);

			struct mems mems = { 0 };
			int excess;

			ret = mems_ls(&mems);
			INTERR(ret, "mems_ls returned %d\n", ret);

			excess = mems.num_mem_chunks - 4;
			if (excess > 0) {
				ret = mems_shift(&mems, excess);
				INTERR(ret, "mems_shift returned %d\n", ret);
			}

			mems_fill(&mems, 1UL << 30);

			ret = ihk_reserve_mem(0, mems.mem_chunks,
					      mems.num_mem_chunks);
			INTERR(ret, "ihk_reserve_mem returned %d\n", ret);

			ret = ihk_create_os(0);
			INTERR(ret, "ihk_create_os returned %d\n", ret);

			ret = mems_os_assign();
			INTERR(ret, "mems_os_assign returned %d\n", ret);

			exit(0);
			break; }
		case 'r': {
			previleged = 1;

			struct mems mems_after_assign = { 0 };
			struct mems margin = { 0 };
			int excess;

			ret = mems_ls(&mems_after_assign);
			INTERR(ret, "mems_ls returned %d\n", ret);

			excess = mems_after_assign.num_mem_chunks - 4;
			if (excess > 0) {
				ret = mems_shift(&mems_after_assign, excess);
				INTERR(ret, "mems_shift returned %d\n", ret);
			}

			mems_fill(&mems_after_assign, 1UL << 30);

			excess = mems_after_assign.num_mem_chunks - 4;
			if (excess > 0) {
				ret = mems_shift(&mems_after_assign, excess);
				INTERR(ret, "mems_shift returned %d\n", ret);
			}

			ret = mems_copy(&margin, &mems_after_assign);
			INTERR(ret, "mems_copy returned %d\n", ret);

			mems_fill(&margin, 4UL << 20);

			/* Check if there's no side effects */
			ret = mems_check_assigned(&mems_after_assign,
						  &margin);
			OKNG(ret == 0, "assigned as expected\n");

			/* Clean up */
			ret = mems_os_release();
			INTERR(ret, "mems_os_release returned %d\n", ret);

			ret = ihk_destroy_os(0, 0);
			INTERR(ret, "ihk_destroy_os returned %d\n", ret);

			ret = mems_release();
			INTERR(ret, "mems_release returned %d\n", ret);

			ret = linux_rmmod(1);
			INTERR(ret, "rmmod returned %d\n", ret);

			exit(0);
			break; }
		default: /* '?' */
			printf("unknown option %c\n", optopt);
			exit(1);
		}
	}

	struct mems mems_input[1] = {{ 0 }};
	int ret_expected[] = { -EPERM };

	/* Activate and check */
	for (i = 0; i < 1; i++) {
		START("test-case: user privilege: %s\n", values[i]);

		ret = mems_push(&mems_input[i], -1, 0);
		INTERR(ret, "mems_push returned %d\n", ret);

		ret = linux_wait_chmod(0);
		INTERR(ret, "device file mode didn't change to 0666\n");

		ret = ihk_os_release_mem(0, mems_input[i].mem_chunks,
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
		linux_rmmod(1);
	}
	return ret;
}
