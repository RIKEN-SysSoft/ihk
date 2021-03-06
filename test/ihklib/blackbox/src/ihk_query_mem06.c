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

	while ((opt = getopt(argc, argv, "ir")) != -1) {
		switch (opt) {
		case 'i':
			/* Precondition */
			ret = linux_insmod(0);
			INTERR(ret, "linux_insmod returned %d\n", ret);

			ret = mems_reserve();
			INTERR(ret, "mems_reserve returned %d\n", ret);
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

	struct mems mems_input[1] = {{ 0 }};

	int ret_expected[] = { -EACCES };
	int ret_expected_get_num_reserved[] = { -EACCES };

	/* Activate and check */
	for (i = 0; i < 1; i++) {
		int num_mem_chunks;

		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_get_num_reserved_mem_chunks(0);
		INTERR(ret != ret_expected_get_num_reserved[i],
			"ihk_get_num_reserved_mems returned %d\n", ret);
		num_mem_chunks = (ret < 0) ? 0 : ret;

		ret = mems_init(&mems_input[0], num_mem_chunks);
		INTERR(ret, "mems_init returned %d\n", ret);

		ret = ihk_query_mem(0, mems_input[i].mem_chunks,
				    mems_input[i].num_mem_chunks);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);
	}

	ret = 0;
 out:
	return ret;
}
