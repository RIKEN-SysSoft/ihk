#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <ihklib.h>
#include <ihk/ihklib_private.h>
#include "util.h"
#include "okng.h"
#include "cpu.h"
#include "mem.h"
#include "os.h"
#include "user.h"
#include "params.h"
#include "linux.h"

#define MAX_COUNT 10

const char param[] = "user privilege";
const char *values[] = {
	"non-root",
};

int main(int argc, char **argv)
{
	int ret;
	int i;
	int opt;
	unsigned long os_set[1] = { 1 };

	params_getopt(argc, argv);

	while ((opt = getopt(argc, argv, "ir")) != -1) {
		switch (opt) {
		case 'i':
			/* Precondition */
			ret = linux_insmod(0);
			INTERR(ret, "linux_insmod returned %d\n", ret);

			ret = cpus_reserve();
			INTERR(ret, "cpus_reserve returned %d\n", ret);

			ret = mems_reserve();
			INTERR(ret, "mems_reserve returned %d\n", ret);

			ret = ihk_create_os(0);
			INTERR(ret, "ihk_create_os returned %d\n", ret);

			ret = cpus_os_assign();
			INTERR(ret, "cpus_os_assign returned %d\n", ret);

			ret = mems_os_assign();
			INTERR(ret, "mems_os_assign returned %d\n", ret);

			ret = os_load();
			INTERR(ret, "os_load returned %d\n", ret);

			ret = os_kargs();
			INTERR(ret, "os_kargs returned %d\n", ret);

			ret = ihk_os_boot(0);
			INTERR(ret, "ihk_os_boot returned %d\n", ret);

			ret = os_wait_for_status(IHK_STATUS_RUNNING);
			INTERR(ret, "os_wait_for_status timeout %d\n", ret);

			exit(0);
			break;
		case 'r':
			ret = os_wait_for_status(IHK_STATUS_FROZEN);
			OKNG(ret == -ETIMEDOUT,
				"os status didn't changed to FROZEN as expected\n");

			/* should never be true in this case*/
			if (ihk_os_get_status(0) == IHK_STATUS_FROZEN) {
				int n = sizeof(unsigned long) * 8;

				INFO("trying to thaw...\n");
				ret = ihk_os_thaw(os_set, n);
				INTERR(ret, "ihk_os_thaw returned %d\n", ret);
			}

			ret = ihk_os_shutdown(0);
			INTERR(ret, "ihk_os_shutdown returned %d\n", ret);

			ret = os_wait_for_status(IHK_STATUS_INACTIVE);
			INTERR(ret, "os status didn't change to %d\n",
			       IHK_STATUS_INACTIVE);

			ret = cpus_os_release();
			INTERR(ret, "cpus_os_release returned %d\n", ret);

			ret = mems_os_release();
			INTERR(ret, "mems_os_release returned %d\n", ret);

			ret = ihk_destroy_os(0, 0);
			INTERR(ret, "ihk_destroy_os returned %d\n", ret);

			ret = cpus_release();
			INTERR(ret, "cpus_release returned %d\n", ret);

			ret = mems_release();
			INTERR(ret, "mems_release returned %d\n", ret);

			ret = linux_rmmod(0);
			INTERR(ret, "rmmod returned %d\n", ret);
			exit(0);
			break;
		default: /* '?' */
			printf("unknown option %c\n", optopt);
			exit(1);
		}
	}

	int ret_expected[1] = { -EPERM };

	/* Activate and check */
	for (i = 0; i < 1; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		ret = linux_wait_chmod(0);
		INTERR(ret, "device file mode didn't change to 0666\n");

		INFO("trying to freeze os\n");
		ret = ihk_os_freeze(os_set, 8 * sizeof(unsigned long));
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);
	}

	ret = 0;
 out:
	if (opt == 'i' || opt == 'r') {
		if (ihk_get_num_os_instances(0)) {
			if (ihk_os_get_status(0) == IHK_STATUS_FROZEN) {
				ihk_os_thaw(os_set, sizeof(unsigned long) * 8);
				os_wait_for_status(IHK_STATUS_RUNNING);
			}
			ihk_os_shutdown(0);
			os_wait_for_status(IHK_STATUS_INACTIVE);
			cpus_os_release();
			mems_os_release();
			ihk_destroy_os(0, 0);
		}
		cpus_release();
		mems_release();
		linux_rmmod(1);
	}
	return ret;
}
