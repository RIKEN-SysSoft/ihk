#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "mem.h"
#include "params.h"
#include "linux.h"

const char param[] = "mem_chunks to store result";
const char *values[] = {
	"NULL",
	"assigned",
	"assigned + 1",
	"assigned - 1",
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

	ret = ihk_create_os(0);
	INTERR(ret, "ihk_create_os returned %d\n", ret);

	struct mems mems_input[4] = { 0 };

	for (i = 1; i < 4; i++) {
		ret = mems_reserved(&mems_input[i]);
		INTERR(ret, "mems_reserved returned %d\n", ret);
	}

	/* Plus one */
	ret = mems_push(&mems_input[2],
			mems_input[2].mem_chunks[0].size,
			mems_input[2].mem_chunks[0].numa_node_number);
	INTERR(ret, "mems_push returned %d\n", ret);

	/* Minus one */
	ret = mems_pop(&mems_input[3], 1);
	INTERR(ret, "mems_pop returned %d\n", ret);

	struct mems mems_after_assign[4] = { 0 };

	ret = mems_reserved(&mems_after_assign[1]);
	INTERR(ret, "mems_reserved returned %d\n", ret);

	/* Minus one */
	ret = mems_reserved(&mems_after_assign[3]);
	INTERR(ret, "mems_reserved returned %d\n", ret);
	ret = mems_pop(&mems_after_assign[3], 1);
	INTERR(ret, "mems_pop returned %d\n", ret);

	int ret_expected[] = {
		-EINVAL,
		0,
		-EINVAL,
		-EINVAL,
	};

	struct mems *mems_expected[] = {
		NULL,
		&mems_after_assign[1],
		NULL,
		NULL,
	};

	/* Precondition */
	ret = mems_os_assign();
	INTERR(ret, "mems_os_assign returned %d\n", ret);

	/* Activate and check */
	for (i = 0; i < 4; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_os_query_mem(0, mems_input[i].mem_chunks,
				mems_input[i].num_mem_chunks);
		OKNG(ret == ret_expected[i],
			"return value: %d, expected: %d\n",
			ret, ret_expected[i]);

		if (mems_expected[i]) {
			ret = mems_compare(&mems_input[i],
					   mems_expected[i], NULL);
			OKNG(ret == 0, "query result matches input\n");
		}
	}

	ret = 0;
 out:
	mems_os_release();
	ihk_destroy_os(0, 0);
	mems_release();
	linux_rmmod(0);
	return ret;
}

