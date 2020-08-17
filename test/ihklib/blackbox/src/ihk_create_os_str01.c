#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "cpu.h"
#include "params.h"
#include "linux.h"

const char param[] = "existence of IHK device file";
const char *values[] = {
	"without IHK device file",
	"with IHK device file",
};

const char params[] =
	"MCK_CPUS=12-35\0"
	"MCK_MEM=1G@4\0"
	"MCK_IKC_MAP=12-23:0+24-35:1\0"
	"MCK_KARGS=hidos allow_oversubscribe\0";
const char default_kargs[] = "hidos allow_oversubscribe time_sharing";
char err_msg[IHKLIB_MAX_SIZE_ERR_MSG];

int main(int argc, char **argv)
{
	params_getopt(argc, argv);

	int ret_expected[] = { -ENOENT, 0 };
	enum ihklib_os_status status_expected[] = {
		IHK_STATUS_INACTIVE,
		IHK_STATUS_RUNNING,
	};
	char kmsg_input[2][IHK_KMSG_SIZE] = { { 0 } };
	char kernel_image[4096];
	int os_index = -1;

	sprintf(kernel_image, "%s/%s/kernel/mckernel.img",
		QUOTE(WITH_MCK), QUOTE(BUILD_TARGET));

	for (i = 0; i < 2; i++) {
		char *kmsg = NULL;

		START("test-case: %s: %s\n", param, values[i]);

		/* Precondition */
		if (i == 1) {
			ret = linux_insmod(0);
			INTERR(ret, "linux_insmod returned %d\n", ret);

		}

		ret = ihk_create_os_str(0, &os_index,
					params, 4,
					kernel_image,
					default_kargs,
					&err_msg);
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
