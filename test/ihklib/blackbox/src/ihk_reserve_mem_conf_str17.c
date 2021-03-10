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

const char param[] = "define none after defining all";
const char *values[] = {
	"defines all to non-default values",
	"defines none (ihklib.c defines all to default values)"
};

#define NR_CASES (sizeof(values) / sizeof(values[0]))

const char *env_str[] = {
	"IHK_RESERVE_MEM_BALANCED_ENABLE=1\0"
	"IHK_RESERVE_MEM_BALANCED_BEST_EFFORT=1\0"
	"IHK_RESERVE_MEM_BALANCED_VARIANCE_LIMIT=10\0"
	"IHK_RESERVE_MEM_MIN_CHUNK_SIZE=131072\0"
	"IHK_RESERVE_MEM_MAX_SIZE_RATIO_ALL=90\0"
	"IHK_RESERVE_MEM_TIMEOUT=60\0",
	"DUMMY_VARIABLE=0\0"
};

const int nenv[] = { 6, 1 };

extern void dump_reserve_mem_conf(void); /* defined in patch */

int main(int argc, char **argv)
{
	int ret;
	int subindex = -1;
	int opt;

	opterr = 0;
	while ((opt = getopt(argc, argv, "i:c")) != -1) {
		switch (opt) {
		case 'i':
			subindex = atoi(optarg);
			break;
		default:
			INTERR(1, "unknown option %c\n", opt);
			break;
		}
	}
	opterr = 1;

	ARRAY_SIZE_CHECK(values, NR_CASES);
	ARRAY_SIZE_CHECK(env_str, NR_CASES);
	ARRAY_SIZE_CHECK(nenv, NR_CASES);

	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	START("test-case: %s: %s\n", param, values[subindex]);

	ret = ihk_reserve_mem_conf_str(0,
				       env_str[subindex],
				       nenv[subindex]);
	INTERR(ret, "ihk_reserve_mem_conf_str failed with %d\n",
	     ret);

	if (subindex == 1) {
		dump_reserve_mem_conf();
	}

	ret = 0;
out:
	linux_rmmod(0);

	return ret;
}
