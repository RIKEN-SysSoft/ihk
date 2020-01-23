#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "params.h"
#include "linux.h"
#include <unistd.h>

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

	int ret_expected[2] = { -ENOENT, 0 };
	int ret_expected_os_instances[2] = { -ENOENT, 1 };

	for (i = 0; i < 2; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_create_os(0);
		OKNG(ret == ret_expected[i],
		     "return value (os index when positive): %d, expected: %d\n",
		     ret, ret_expected[i]);

		if (ret_expected[i] == 0) {
			ret = ihk_get_num_os_instances(0);
			OKNG(ret == ret_expected_os_instances[i],
			     "# of os instances: %d, expected: %d\n",
			     ret, ret_expected_os_instances[i]);
		}

		/* Precondition */
		if (i == 0) {
			ret = linux_insmod(0);
			INTERR(ret, "linux_insmod returned %d\n", ret);
		}
	}

	ret = 0;
out:
	linux_rmmod(0);
	return ret;
}

