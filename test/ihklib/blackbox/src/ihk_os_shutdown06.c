#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "cpu.h"
#include "mem.h"
#include "os.h"
#include "params.h"
#include "linux.h"

const char param[] = "user privilege";
const char *messages[] = {
	"non-root",
};

int main(int argc, char **argv)
{
	int ret;
	int i;

	params_getopt(argc, argv);

	int ret_expected[] = {
		-EPERM,
	};

	enum ihklib_os_status status_expected[] = {
		IHK_STATUS_RUNNING,
	};

	/* Parse additional options */
	int opt;

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

			/* make /dev/mcos0 accessible to non-root */
			ret = linux_chmod(0);
			INTERR(ret, "linux_chmod returned %d\n", ret);

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

			exit(0);
			break;
		case 'r':
			/* check if os survived */
			os_wait_for_status(status_expected[0]);
			ret = ihk_os_get_status(0);
			OKNG(ret == status_expected[0],
			     "status: %d, expected: %d\n",
			     ret, status_expected[0]);

			/* Clean up */
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

			ret = linux_rmmod(1);
			INTERR(ret, "rmmod returned %d\n", ret);
			exit(0);
			break;
		default: /* '?' */
			printf("unknown option %c\n", optopt);
			exit(1);
		}
	}

	/* Activate and check */
	for (i = 0; i < 1; i++) {
		START("test-case: %s: %s\n", param, messages[i]);

		INFO("trying to shutdown os\n");
		ret = ihk_os_shutdown(0);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		if (ret_expected[i] == 0) {
			ret = os_wait_for_status(IHK_STATUS_INACTIVE);
			INTERR(ret, "os status didn't change to %d\n",
			       IHK_STATUS_INACTIVE);
		}
	}

	ret = 0;
 out:
	if (opt == 'i' || opt == 'r') {
		if (ihk_get_num_os_instances(0)) {
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
