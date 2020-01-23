#include <limits.h>
#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "mem.h"
#include "params.h"
#include "linux.h"

const char param[] = "exsitence of IHK device file";
const char *values[] = {
	"without IHK device file",
	"with IHK device file",
};

int main(int argc, char **argv)
{
	int ret;
	int i;

	params_getopt(argc, argv);

	int mem_conf_keys[2] = {
		IHK_RESERVE_MEM_MAX_SIZE_RATIO_ALL,
		IHK_RESERVE_MEM_MAX_SIZE_RATIO_ALL
	};

	int mem_conf_values[2] = {
		95,
		95
	};

	int ret_expected[2] = {
		-ENOENT,
		0
	};

	/* Activate and check */
	for (i = 0; i < 2; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		if (i == 1) {
			/* Precondition */
			ret = linux_insmod(0);
			INTERR(ret, "linux_insmod returned %d\n", ret);
		}

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
