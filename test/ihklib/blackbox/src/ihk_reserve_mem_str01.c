#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ihklib.h>
#include <ihk/ihklib_private.h>
#include "util.h"
#include "okng.h"
#include "cpu.h"
#include "mem.h"
#include "os.h"
#include "params.h"
#include "linux.h"

const char param[] = "existence of IHK device file";
const char *values[] = {
	"without IHK device file",
	"with IHK device file",
};

const char *env_str[] = {
#if NR_NUMA == 1
	"IHK_MEM=1G@0\0",
#elif FIRST_USER_NUMA == 4
	"IHK_MEM=1G@4,512M@5\0",
#else
	"IHK_MEM=1G@0,512M@1\0",
#endif
#if NR_NUMA == 1
	"IHK_MEM=1G@0\0",
#elif FIRST_USER_NUMA == 4
	"IHK_MEM=1G@4,512M@5\0",
#else
	"IHK_MEM=1G@0,512M@1\0",
#endif
};

const int ret_expected[] = { -ENOENT, 0 };
const int nr_chunks_expected[] = {
	-ENOENT,
#if NR_NUMA == 1
	1
#else
	2
#endif
};

#define NR_CASES (sizeof(values) / sizeof(values[0]))

int main(int argc, char **argv)
{
	int j;
	int ret;
	int subindex = -1;
	int opt;

	opterr = 0;
	while ((opt = getopt(argc, argv, "i:c")) != -1) {
		switch (opt) {
		case 'i':
			subindex = atoi(optarg);
			break;
		case 'c': /* clean up */
			ret = 0;
			goto out;
		default:
			INTERR(1, "unknown option %c\n", opt);
			break;
		}
	}
	opterr = 1;

	INTERR(subindex == -1, "-i <subindex> is not specified\n");

	struct mems mems[NR_CASES] = { { 0 } };
	struct mems mems_margin[NR_CASES] = { { 0 } };

#if NR_NUMA == 1
	ret = mems_init(&mems[1], 1);
	INTERR(ret, "mems_init returned %d\n", ret);

	mems[1].mem_chunks[0].size = 1UL << 30;
	mems[1].mem_chunks[0].numa_node_number = 0;
#else
	ret = mems_init(&mems[1], 2);
	INTERR(ret, "mems_init returned %d\n", ret);

	mems[1].mem_chunks[0].size = 1UL << 30;
	mems[1].mem_chunks[1].size = 1UL << 29;

	for (j = 0; j < 2; j++) {
#if FIRST_USER_NUMA == 4
		mems[1].mem_chunks[j].numa_node_number = 4 + j;
#else
		mems[1].mem_chunks[j].numa_node_number = j;
#endif
	}
#endif

	ret = mems_copy(&mems_margin[1], &mems[1]);
	INTERR(ret, "mems_copy returned %d\n", ret);

	mems_fill(&mems_margin[1], 4UL << 20);

	ARRAY_SIZE_CHECK(values, NR_CASES);
	ARRAY_SIZE_CHECK(env_str, NR_CASES);
	ARRAY_SIZE_CHECK(ret_expected, NR_CASES);
	ARRAY_SIZE_CHECK(nr_chunks_expected, NR_CASES);
	ARRAY_SIZE_CHECK(mems, NR_CASES);
	ARRAY_SIZE_CHECK(mems_margin, NR_CASES);

	START("test-case: %s: %s\n", param, values[subindex]);

	/* Precondition */
	if (subindex == 1) {
		ret = linux_insmod(0);
		INTERR(ret, "linux_insmod returned %d\n", ret);
	}

	ret = ihk_reserve_mem_str(0, env_str[subindex], 1);

	OKNG(ret == ret_expected[subindex],
	     "return value: %d, expected: %d\n",
	     ret, ret_expected[subindex]);

	ret = ihk_get_num_reserved_mem_chunks(0);
	OKNG(ret == nr_chunks_expected[subindex],
	     "# of chunks reserved: actual %d, expected %d\n",
	     ret, nr_chunks_expected[subindex]);

	if (subindex == 1) {
		ret = mems_check_reserved(&mems[subindex],
					  &mems_margin[subindex]);
		OKNG(ret == 0, "mem: reserved as expected\n");
	}

	/* let the driver script check the kmsg */
	return 0;

 out:
	if (ihk_get_num_os_instances(0) > 0) {
		ihk_os_shutdown(0);
		os_wait_for_status(IHK_STATUS_INACTIVE);
		cpus_os_release();
		mems_os_release();
		ihk_destroy_os(0, 0);
	}
	cpus_release();
	mems_release();
	linux_rmmod(1);

	return ret;
}
