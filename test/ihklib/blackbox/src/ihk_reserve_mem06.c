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

	params_getopt(argc, argv);

	/* Parse additional options */
	int opt;

	struct mems mems_input[1] = {{ 0 }};
	struct mems mems_after_reserve[1] = {{ 0 }};

	/* Both Linux and McKernel cpus */
	for (i = 0; i < 1; i++) {
		int excess;

		ret = mems_ls(&mems_input[i]);
		INTERR(ret, "mems_ls returned %d\n", ret);

		excess = mems_input[i].num_mem_chunks - 4;
		if (excess > 0) {
			ret = mems_shift(&mems_input[i], excess);
			INTERR(ret, "mems_ls returned %d\n", ret);
		}
	}

	int ret_expected[] = { -EACCES };
	struct mems *mems_expected[] = {
		&mems_after_reserve[0]
	};

	while ((opt = getopt(argc, argv, "ir")) != -1) {
		switch (opt) {
		case 'i':
			/* Precondition */
			ret = linux_insmod(0);
			INTERR(ret, "linux_insmod returned %d\n", ret);
			exit(0);
			break;
		case 'r':
			/* Check there's no side effects */
			if (mems_expected[0]) {
				ret = mems_check_reserved(mems_expected[0],
							  NULL);
				OKNG(ret == 0, "reserved as expected\n");
			}

			/* Clean up */
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

		ret = ihk_reserve_mem(0, mems_input[i].mem_chunks,
				      mems_input[i].num_mem_chunks);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);
	}

	ret = 0;
 out:
	return ret;
}
