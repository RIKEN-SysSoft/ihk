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

const char param[] = "valid env";
const char *values[] = {
	"IHK_RESERVE_MEM_*=...",
};

#define NR_CASES (sizeof(values) / sizeof(values[0]))

const char *env_str[] = {
	"IHK_RESERVE_MEM_BALANCED_ENABLE=1\0"
	"IHK_RESERVE_MEM_BALANCED_BEST_EFFORT=1\0"
	"IHK_RESERVE_MEM_BALANCED_VARIANCE_LIMIT=10\0"
	"IHK_RESERVE_MEM_MIN_CHUNK_SIZE=65536\0"
	"IHK_RESERVE_MEM_MAX_SIZE_RATIO_ALL=98\0"
	"IHK_RESERVE_MEM_TIMEOUT=30\0"
};

const int ret_expected[NR_CASES] = { 0 };

int main(int argc, char **argv)
{
	int i;
	int ret;

	ARRAY_SIZE_CHECK(values, NR_CASES);
	ARRAY_SIZE_CHECK(env_str, NR_CASES);
	ARRAY_SIZE_CHECK(ret_expected, NR_CASES);

	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	for (i = 0; i < NR_CASES; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_reserve_mem_conf_str(0, env_str[i], 6);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);
	}

 out:
	linux_rmmod(0);

	return ret;
}
