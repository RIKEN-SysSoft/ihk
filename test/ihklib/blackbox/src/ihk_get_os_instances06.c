#include <stdlib.h>
#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "params.h"
#include "linux.h"
#include <unistd.h>

#define INDEX_DUMMY -0x80000000

const char param[] = "user privilege";
const char *values[] = {
	"non-root",
};

int main(int argc, char **argv)
{
	int ret;
	int i;
	int opt;
	int num_os_instances = 0;

	params_getopt(argc, argv);


	while ((opt = getopt(argc, argv, "irn:")) != -1) {
		switch (opt) {
		case 'i':
			/* Precondition */
			ret = linux_insmod(0);
			INTERR(ret, "linux_insmod returned %d\n", ret);

			ret = ihk_create_os(0);
			INTERR(ret, "ihk_create_os returned %d\n", ret);

			ret = ihk_get_num_os_instances(0);
			INTERR(ret != 1, "ihk_create_os returned %d\n", ret);
			num_os_instances = ret;

			return num_os_instances;

			break;
		case 'r':
			ret = ihk_destroy_os(0, 0);
			INTERR(ret, "ihk_destroy_os returned %d\n", ret);

			ret = linux_rmmod(1);
			INTERR(ret, "rmmod returned %d\n", ret);
			exit(0);
			break;
		case 'n':
			ret = atoi(optarg);
			INTERR(ret < 0,
				"Invalid number of os instance: %d\n",
				ret);
			num_os_instances = ret;
			break;
		default: /* '?' */
			printf("unknown option %c\n", optopt);
			exit(1);
		}
	}

	int ret_expected[1] = { -EACCES };
	int index_input[1] = { INDEX_DUMMY };
	int index_expected[1] = { INDEX_DUMMY };


	for (i = 0; i < 1; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_get_os_instances(0, index_input, num_os_instances);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);
		OKNG(index_input[i] == index_expected[i],
			"get os index as expected\n");
	}

out:
	return ret;
}
