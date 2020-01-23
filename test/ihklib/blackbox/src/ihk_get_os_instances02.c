#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "params.h"
#include "linux.h"
#include <unistd.h>

#define INDEX_DUMMY -0x80000000

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

	int ret_expected[2] = {
		-ENOENT,
		0,
	};

	int index_input[2][1] = {
		{ INDEX_DUMMY },
		{ INDEX_DUMMY },
	};

	int index_expected[2][1] = {
		{ INDEX_DUMMY },
		{ 0 },
	};
	for (i = 0; i < 2; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		if (i == 1) {
			ret = linux_insmod(0);
			INTERR(ret, "linux_insmod returned %d\n", ret);

			ret = ihk_create_os(0);
			INTERR(ret, "ihk_create_os returned %d\n", ret);
		}

		ret = ihk_get_os_instances(0, index_input[i], 1);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);
		OKNG(index_input[i][0] == index_expected[i][0],
		     "get os index as expected\n");
	}

out:
	linux_rmmod(0);
	return ret;
}
