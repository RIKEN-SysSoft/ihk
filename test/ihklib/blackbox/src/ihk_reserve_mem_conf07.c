#include <errno.h>
#include <ihklib.h>
#include <ihk/ihklib_private.h>
#include <limits.h>
#include "util.h"
#include "okng.h"
#include "mem.h"
#include "params.h"
#include "linux.h"

const char param[] = "IHK_RESERVE_MEM_MAX_SIZE_RATIO_ALL";
const char *values[] = {
	"INT_MIN",
	"-1",
	"0",
	"1",
	"IHK_RESERVE_MEM_MAX_SIZE_RATIO_ALL_LIMIT - 1",
	"IHK_RESERVE_MEM_MAX_SIZE_RATIO_ALL_LIMIT",
	"IHK_RESERVE_MEM_MAX_SIZE_RATIO_ALL_LIMIT + 1",
	"INT_MAX",
};

const int mem_conf_values[] = {
	INT_MIN,
	-1,
	0,
	1,
	IHK_RESERVE_MEM_MAX_SIZE_RATIO_ALL_LIMIT - 1,
	IHK_RESERVE_MEM_MAX_SIZE_RATIO_ALL_LIMIT,
	IHK_RESERVE_MEM_MAX_SIZE_RATIO_ALL_LIMIT + 1,
	INT_MAX,
};

const int ret_expected[] = {
	-EINVAL,
	-EINVAL,
	-EINVAL,
	0,
	0,
	0,
	-EINVAL,
	-EINVAL,
};

int main(int argc, char **argv)
{
	int ret;
	int i;
	int nr_cases = sizeof(values) / sizeof(values[0]);

	ARRAY_SIZE_CHECK(values, nr_cases);
	ARRAY_SIZE_CHECK(mem_conf_values, nr_cases);
	ARRAY_SIZE_CHECK(ret_expected, nr_cases);

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	/* Activate and check */
	for (i = 0; i < nr_cases; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_reserve_mem_conf(0,
					   IHK_RESERVE_MEM_MAX_SIZE_RATIO_ALL,
					   (void *)&mem_conf_values[i]);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);
	}

	ret = 0;
 out:
	linux_rmmod(0);
	return ret;
}
