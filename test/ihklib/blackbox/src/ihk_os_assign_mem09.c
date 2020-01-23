#define _GNU_SOURCE
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "mem.h"
#include "params.h"
#include "linux.h"

const char param[] = "Amount of memory per NUMA Node";
const char *values[] = {
	"Reserved",
	"Reserved + one chunk per node",
	"Reserved - one chunk per node",
	"Reserved with last chunk halved",
};

static int node_last_chunk(int node, struct mems *mems_p)
{
	int _node = node;
	struct mems *mems = mems_p;
	int num_chunks = mems->num_mem_chunks;
	int iter, idx = 0;

	for (iter = 0; iter < num_chunks; iter++) {
		if (mems->mem_chunks[iter].numa_node_number == _node) {
			idx = iter;
		}
	}
	return idx;
}

int mems_remove_chunk(struct mems *mems, int idx)
{
	int ret;
	int num_mem_chunks = mems->num_mem_chunks;

	if (idx < 0 || idx >= num_mem_chunks || mems->mem_chunks == NULL) {
		ret = 1;
		goto out;
	}

	if (idx != (num_mem_chunks - 1)) {
		memmove(mems->mem_chunks + idx, mems->mem_chunks + idx + 1,
			sizeof(struct ihk_mem_chunk) *
			(mems->num_mem_chunks - (idx + 1)));
	}

	mems->mem_chunks = mremap(mems->mem_chunks,
				  sizeof(struct ihk_mem_chunk) *
				  mems->num_mem_chunks,
				  sizeof(struct ihk_mem_chunk) *
				  (mems->num_mem_chunks - 1),
				  MREMAP_MAYMOVE);
	if (mems->mem_chunks == MAP_FAILED) {
		ret = -errno;
		goto out;
	}

	mems->num_mem_chunks -= 1;
	ret = 0;
out:
	return ret;
}

int main(int argc, char **argv)
{
	int ret;
	int i, j;
	unsigned long nodemask = 0;
	unsigned long maxnode = sizeof(unsigned long) * 8;
	int excess;

	params_getopt(argc, argv);

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	/* Create node mask */
	struct mems mems_ref = { 0 };

	ret = mems_ls(&mems_ref, "MemFree", 1.0);
	INTERR(ret, "mems_ls returned %d\n", ret);

	excess = mems_ref.num_mem_chunks - 4;
	if (excess > 0) {
		ret = mems_shift(&mems_ref, excess);
		INTERR(ret, "mems_shift returned %d\n", ret);
	}

	for (i = 0; i < mems_ref.num_mem_chunks; i++) {
		nodemask |=
			(1UL << mems_ref.mem_chunks[i].numa_node_number);
	}

	ret = mems_reserve();
	INTERR(ret, "mems_reserve returned %d\n", ret);

	ret = ihk_create_os(0);
	INTERR(ret, "ihk_create_os returned %d\n", ret);

	struct mems mems_input[4] = {{ 0 }};
	struct mems mems_after_assign[4] = {{ 0 }};
	struct mems mems_margin[4] = {{ 0 }};

	for (i = 0; i < 4; i++) {
		ret = mems_reserved(&mems_input[i]);
		INTERR(ret, "mems_reserved returned %d\n", ret);

		ret = mems_reserved(&mems_after_assign[i]);
		INTERR(ret, "mems_reserved returned %d\n", ret);

		ret = mems_copy(&mems_margin[i], &mems_after_assign[i]);
		INTERR(ret, "mems_copy returned %d\n", ret);

		mems_fill(&mems_margin[i], 4UL << 20);
	}

	/* plus one */
	for (j = 0; j < maxnode; j++) {
		if (!(nodemask & (1UL << j))) {
			continue;
		}

		int idx = node_last_chunk(j, &mems_input[1]);

		ret = mems_push(&mems_input[1],
				mems_input[1].mem_chunks[idx].size, j);
		INTERR(ret, "mems_push returned %d\n", ret);
	}

	ret = mems_shift(&mems_after_assign[1],
			mems_after_assign[1].num_mem_chunks);
	INTERR(ret, "mems_shift returned %d\n", ret);

	/* minus one */
	for (j = 0; j < maxnode; j++) {
		int idx;

		if (!(nodemask & (1UL << j))) {
			continue;
		}

		idx = node_last_chunk(j, &mems_input[2]);

		ret = mems_remove_chunk(&mems_input[2], idx);
		INTERR(ret, "mems_remove_chunk returned %d\n", ret);

		ret = mems_remove_chunk(&mems_after_assign[2], idx);
		INTERR(ret, "mems_remove_chunk returned %d\n", ret);
	}

	/* last chunk halved */
	for (j = 0; j < maxnode; j++) {
		if (!(nodemask & (1UL << j))) {
			continue;
		}

		int idx = node_last_chunk(j, &mems_input[3]);

		mems_input[3].mem_chunks[idx].size /= 2;
		mems_after_assign[3].mem_chunks[idx].size /= 2;
	}

	for (i = 0; i < 4; i++) {
		INFO("test-input: %s\n", values[i]);
		for (j = 0; j < mems_input[i].num_mem_chunks; j++) {
			INFO("node id: %d, size: %ld (%ld MiB)\n",
			     mems_input[i].mem_chunks[j].numa_node_number,
			     mems_input[i].mem_chunks[j].size,
			     mems_input[i].mem_chunks[j].size >> 20);
		}
	}

	int ret_expected[4] = {
		0,
		-ENOMEM,
		0,
		0,
	};

	struct mems *mems_expected[4] = {
		&mems_after_assign[0],
		&mems_after_assign[1],
		&mems_after_assign[2],
		&mems_after_assign[3],
	};

	for (i = 0; i < 4; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_os_assign_mem(0, mems_input[i].mem_chunks,
				mems_input[i].num_mem_chunks);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		if (mems_expected[i]) {
			ret = mems_check_assigned(mems_expected[i],
						  &mems_margin[i]);
			OKNG(ret == 0, "assigned as expected.\n");

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

