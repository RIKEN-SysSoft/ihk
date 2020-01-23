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

const char param[] = "existence of OS instance";
const char *messages[] = {
	"wihout OS instance",
	"with OS instance",
};

int main(int argc, char **argv)
{
	int ret;
	int i;

	params_getopt(argc, argv);

	int ret_expected[] = { -ENOENT, 0 };

	enum ihklib_os_status status_expected[] = {
		IHK_STATUS_INACTIVE,
		IHK_STATUS_RUNNING,
	};

	char kmsg_input[2][IHK_KMSG_SIZE] = { 0 };

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	ret = cpus_reserve();
	INTERR(ret, "cpus_reserve returned %d\n", ret);

	ret = mems_reserve();
	INTERR(ret, "mems_reserve returned %d\n", ret);

	/* Activate and check */
	for (i = 0; i < 2; i++) {
		char *kmsg = NULL;

		START("test-case: %s: %s\n", param, messages[i]);

		/* Precondition */
		if (i == 1) {
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
		}

		ret = ihk_os_boot(0);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		if (ret == 0) {
			ret = ihk_os_kmsg(0, kmsg_input[i], IHK_KMSG_SIZE);
			INTERR(ret < 0, "ihk_os_kmsg returned %d\n", ret);

			kmsg = strstr(kmsg_input[i], "booted");
			OKNG(kmsg != NULL,
			     "expected string found in kmsg\n");
		}

		/* Check OS status and clean up*/
		if (ihk_get_num_os_instances(0)) {
			os_wait_for_status(status_expected[i]);
			ret = ihk_os_get_status(0);
			OKNG(ret == status_expected[i],
			     "status: %d, expected: %d\n",
			     ret, status_expected[i]);

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
		}
	}

	ret = 0;
 out:
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

	return ret;
}
