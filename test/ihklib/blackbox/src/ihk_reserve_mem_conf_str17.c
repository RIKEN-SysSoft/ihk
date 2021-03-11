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

const char param[] = "settings string";
const char *values[] = {
	"sets non-default values to all variables and then reserves memory to get the values back to default"
};

#define NR_CASES (sizeof(values) / sizeof(values[0]))

const char *env_str[] = {
	"IHK_RESERVE_MEM_BALANCED_ENABLE=1\0"
	"IHK_RESERVE_MEM_BALANCED_BEST_EFFORT=1\0"
	"IHK_RESERVE_MEM_BALANCED_VARIANCE_LIMIT=10\0"
	"IHK_RESERVE_MEM_MIN_CHUNK_SIZE=131072\0"
	"IHK_RESERVE_MEM_MAX_SIZE_RATIO_ALL=90\0"
	"IHK_RESERVE_MEM_TIMEOUT=60\0",
};

const int nenv[] = { 6 };

extern void dump_reserve_mem_conf(void); /* defined in patch */

int main(int argc, char **argv)
{
	int ret;

	ARRAY_SIZE_CHECK(values, NR_CASES);
	ARRAY_SIZE_CHECK(env_str, NR_CASES);
	ARRAY_SIZE_CHECK(nenv, NR_CASES);

	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	START("test-case: %s: %s\n", param, values[0]);

	ret = ihk_reserve_mem_conf_str(0,
				       env_str[0],
				       nenv[0]);
	INTERR(ret, "ihk_reserve_mem_conf_str failed with %d\n",
	     ret);

	/* reset to default */
	_mems_reserve(4, 0.9, 1UL << 30);

	dump_reserve_mem_conf();

	ret = 0;
out:
	mems_release();
	linux_rmmod(0);

	return ret;
}
