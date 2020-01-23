#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "params.h"
#include "linux.h"
#include <unistd.h>

const char param[] = "exsitence of OS instance";
const char *values[] = {
	"with no os instance",
	"with one os instance",
};

int main(int argc, char **argv)
{
	int ret;
	int i;

	params_getopt(argc, argv);

	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	int ret_expected[2] = { 0, 1 };

	for (i = 0; i < 2; i++) {
		ret = ihk_get_num_os_instances(0);
		OKNG(ret == ret_expected[i],
		     "# of os instances: %d, expected: %d\n",
		     ret, ret_expected[i]);

		/* Precondition */
		if (i == 0) {
			ret = ihk_create_os(0);
			INTERR(ret, "ihk_create_os returned %d\n", ret);
		}
	}

out:
	linux_rmmod(0);
	return ret;
}
