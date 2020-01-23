#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "mem.h"
#include "params.h"
#include "linux.h"

const char param[] = "num_chunks";
const char *values[] = {
	"0",
	"reserved",
	"reserved + 1",
	"reserved - 1",
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

	struct mems mems_input[] = {
		{
			.mem_chunks = NULL,
			.num_mem_chunks = 0,
		},
		{ 0 },
		{
			.mem_chunks = NULL,
			.num_mem_chunks = 0,
		},
		{ 0 },
	};

	/* Reserved */
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

	struct mems mems_after_assign[4] = {0};

	ret = mems_reserved(&mems_after_assign[1]);
	INTERR(ret, "mems_after_assign returned %d\n", ret);

	ret = mems_reserved(&mems_after_assign[3]);
	INTERR(ret, "mems_after_assign returned %d\n", ret);
	ret = mems_shift(&mems_after_assign[3], 1);
	INTERR(ret, "mems_shift returned %d\n", ret);

	int ret_expected_assign_mem[] = {
		0,
		0,
		-ENOMEM,
		0,
	};

	int ret_expected[] = {
		mems_after_assign[0].num_mem_chunks,
		mems_after_assign[1].num_mem_chunks,
		mems_after_assign[2].num_mem_chunks,
		mems_after_assign[3].num_mem_chunks,
	};

	/* Activate and check */
	for (i = 0; i < 4; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_os_assign_mem(0, mems_input[i].mem_chunks,
				mems_input[i].num_mem_chunks);
		INTERR(ret != ret_expected_assign_mem[i],
		       "ihk_os_assign_mem returned: %d, expected: %d\n",
		       ret, ret_expected_assign_mem[i]);

		ret = ihk_os_get_num_assigned_mem_chunks(0);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		/* Clean up */
		ret = mems_os_release();
		INTERR(ret, "mems_os_release returned %d\n", ret);
	}

	ret = 0;
 out:
	mems_release();
	linux_rmmod(0);
	return ret;
}

