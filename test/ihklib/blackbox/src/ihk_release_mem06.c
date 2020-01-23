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

	struct mems mems_input_reserve_mem[1] = { 0 };

	for (i = 0; i < 1; i++) {
		int excess;

		ret = mems_ls(&mems_input_reserve_mem[i], "MemFree", 0.9);
		INTERR(ret, "mems_ls returned %d\n", ret);

		excess = mems_input_reserve_mem[i].num_mem_chunks - 4;
		if (excess > 0) {
			ret = mems_shift(&mems_input_reserve_mem[i], excess);
			INTERR(ret, "mems_shift returned %d\n", ret);
		}
	}

	/* Parse additional options */
	int opt;

	while ((opt = getopt(argc, argv, "ir")) != -1) {
		switch (opt) {
		case 'i':
			/* Precondition */
			ret = linux_insmod(0);
			INTERR(ret, "linux_insmod returned %d\n", ret);

			ret = ihk_reserve_mem(0,
				      mems_input_reserve_mem[0].mem_chunks,
				      mems_input_reserve_mem[0].num_mem_chunks);
			INTERR(ret, "ihk_reserve_mem returned %d\n", ret);

			exit(0);
			break;
		case 'r':
			/* Clean up */
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

	struct mems mems_input[1] = { 0 };

	int ret_expected[1] = { -EACCES };

	/* Activate and check */
	for (i = 0; i < 1; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		ret = mems_push(&mems_input[i], -1, 0);
		INTERR(ret, "mems_push returned %d\n", ret);

		ret = ihk_release_mem(0, mems_input[i].mem_chunks,
				      mems_input[i].num_mem_chunks);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);
	}

	ret = 0;
 out:
	return ret;
}
