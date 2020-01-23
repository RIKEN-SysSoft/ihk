#include <errno.h>
#include <ihklib.h>
#include <limits.h>
#include "util.h"
#include "okng.h"
#include "mem.h"
#include "params.h"
#include "linux.h"

const char param[] = "<key, value>";
const char *values[] = {
	"<INT_MIN, 0>",
	"<-1, 0>",
	"<IHK_RESERVE_MEM_TOTAL, 10>",
	"<IHK_RESERVE_MEM_MIN_CHUNK_SIZE, 65536>",
	"<IHK_RESERVE_MEM_MAX_SIZE_RATIO_ALL, 95>",
	"<IHK_RESERVE_MEM_TIMEOUT, 300>",
	"<INT_MAX, 0>",
};

int main(int argc, char **argv)
{
	int ret;
	int i;

	params_getopt(argc, argv);

	int mem_conf_keys[] = {
		INT_MIN,
		-1,
		IHK_RESERVE_MEM_TOTAL,
		IHK_RESERVE_MEM_MIN_CHUNK_SIZE,
		IHK_RESERVE_MEM_MAX_SIZE_RATIO_ALL,
		IHK_RESERVE_MEM_TIMEOUT,
		INT_MAX,
	};

	int mem_conf_values[] = {
		0,
		0,
		10,
		65536,
		95,
		300,
		0,
	};

	int ret_expected[] = {
		-EINVAL,
		-EINVAL,
		0,
		0,
		0,
		0,
		-EINVAL,
	};

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	/* Activate and check */
	for (i = 0; i < 7; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_reserve_mem_conf(0, mem_conf_keys[i],
					   &mem_conf_values[i]);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);
	}

	ret = 0;
 out:
	linux_rmmod(0);
	return ret;
}
