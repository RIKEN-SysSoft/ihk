#include <stdlib.h>
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

const char param[] = "parameter string";
const char *values[17] = {
	"MCK_CPUS=,12-23\0",
	"MCK_CPUS=12-23,\0",
	"MCK_MEM=,512M@1\0",
	"MCK_MEM=1G@0,\0",
	"MCK_MEM=@0\0",
	"MCK_MEM=1G@\0",
	"MCK_MEM=1G@,512M@1\0",
	"MCK_MEM=@0,512M@1\0",
	"MCK_IKC_MAP=+12-23:0\0",
	"MCK_IKC_MAP=12-23:0+\0",
	"MCK_IKC_MAP=:0\0",
	"MCK_IKC_MAP=12-23\0",
	"MCK_IKC_MAP=12-23:\0",
	"MCK_IKC_MAP=12-23+24-35:1\0",
	"MCK_IKC_MAP=12-23:+24-35:1\0",
	"MCK_IKC_MAP=,12-23:0\0",
	"MCK_IKC_MAP=12-23,:0\0",
};

const char *env_str[17] = {
	"MCK_CPUS=,12-23\0"
#if FIRST_USER_NUMA == 4
	"MCK_MEM=1G@4,512M@5\0"
#else
	"MCK_MEM=1G@0,512M@1\0"
#endif
	"MCK_IKC_MAP=12:0\0"
	"MCK_KARGS=hidos allow_oversubscribe\0",

	"MCK_CPUS=12-23,\0"
#if FIRST_USER_NUMA == 4
	"MCK_MEM=1G@4,512M@5\0"
#else
	"MCK_MEM=1G@0,512M@1\0"
#endif
	"MCK_IKC_MAP=12:0\0"
	"MCK_KARGS=hidos allow_oversubscribe\0",

	"MCK_CPUS=12-35\0"
	"MCK_MEM=,512M@1\0"
	"MCK_IKC_MAP=12-23:0+24-35:1\0"
	"MCK_KARGS=hidos allow_oversubscribe\0",

	"MCK_CPUS=12-35\0"
	"MCK_MEM=1G@0,\0"
	"MCK_IKC_MAP=12-23:0+24-35:1\0"
	"MCK_KARGS=hidos allow_oversubscribe\0",

	"MCK_CPUS=12-35\0"
	"MCK_MEM=@0\0"
	"MCK_IKC_MAP=12-23:0+24-35:1\0"
	"MCK_KARGS=hidos allow_oversubscribe\0",

	"MCK_CPUS=12-35\0"
	"MCK_MEM=1G@\0"
	"MCK_IKC_MAP=12-23:0+24-35:1\0"
	"MCK_KARGS=hidos allow_oversubscribe\0",

	"MCK_CPUS=12-35\0"
	"MCK_MEM=1G@,512M@1\0"
	"MCK_IKC_MAP=12-23:0+24-35:1\0"
	"MCK_KARGS=hidos allow_oversubscribe\0",

	"MCK_CPUS=12-35\0"
	"MCK_MEM=@0,512M@1\0"
	"MCK_IKC_MAP=12-23:0+24-35:1\0"
	"MCK_KARGS=hidos allow_oversubscribe\0",

	"MCK_CPUS=12-35\0"
#if FIRST_USER_NUMA == 4
	"MCK_MEM=1G@4,512M@5\0"
#else
	"MCK_MEM=1G@0,512M@1\0"
#endif
	"MCK_IKC_MAP=+12-23:0\0"
	"MCK_KARGS=hidos allow_oversubscribe\0",

	"MCK_CPUS=12-35\0"
#if FIRST_USER_NUMA == 4
	"MCK_MEM=1G@4,512M@5\0"
#else
	"MCK_MEM=1G@0,512M@1\0"
#endif
	"MCK_IKC_MAP=12-23:0+\0"
	"MCK_KARGS=hidos allow_oversubscribe\0",

	"MCK_CPUS=12-35\0"
#if FIRST_USER_NUMA == 4
	"MCK_MEM=1G@4,512M@5\0"
#else
	"MCK_MEM=1G@0,512M@1\0"
#endif
	"MCK_IKC_MAP=:0\0"
	"MCK_KARGS=hidos allow_oversubscribe\0",

	"MCK_CPUS=12-23\0"
#if FIRST_USER_NUMA == 4
	"MCK_MEM=1G@4,512M@5\0"
#else
	"MCK_MEM=1G@0,512M@1\0"
#endif
	"MCK_IKC_MAP=12-23\0"
	"MCK_KARGS=hidos allow_oversubscribe\0",

	"MCK_CPUS=12-23\0"
#if FIRST_USER_NUMA == 4
	"MCK_MEM=1G@4,512M@5\0"
#else
	"MCK_MEM=1G@0,512M@1\0"
#endif
	"MCK_IKC_MAP=12-23:\0"
	"MCK_KARGS=hidos allow_oversubscribe\0",

	"MCK_CPUS=12-35\0"
#if FIRST_USER_NUMA == 4
	"MCK_MEM=1G@4,512M@5\0"
#else
	"MCK_MEM=1G@0,512M@1\0"
#endif
	"MCK_IKC_MAP=12-23+24-35:1\0"
	"MCK_KARGS=hidos allow_oversubscribe\0",

	"MCK_CPUS=12-35\0"
#if FIRST_USER_NUMA == 4
	"MCK_MEM=1G@4,512M@5\0"
#else
	"MCK_MEM=1G@0,512M@1\0"
#endif
	"MCK_IKC_MAP=12-23:+24-35:1\0"
	"MCK_KARGS=hidos allow_oversubscribe\0",

	"MCK_CPUS=12-23\0"
#if FIRST_USER_NUMA == 4
	"MCK_MEM=1G@4,512M@5\0"
#else
	"MCK_MEM=1G@0,512M@1\0"
#endif
	"MCK_IKC_MAP=,12-23:0\0"
	"MCK_KARGS=hidos allow_oversubscribe\0",

	"MCK_CPUS=12-35\0"
#if FIRST_USER_NUMA == 4
	"MCK_MEM=1G@4,512M@5\0"
#else
	"MCK_MEM=1G@0,512M@1\0"
#endif
	"MCK_IKC_MAP=12-23,:0\0"
	"MCK_KARGS=hidos allow_oversubscribe\0",
};
const char default_kargs[] = "hidos allow_oversubscribe time_sharing";
const int ret_expected[17] = {
	-EINVAL,
	-EINVAL,
	-EINVAL,
	-EINVAL,
	-EINVAL,
	-EINVAL,
	-EINVAL,
	-EINVAL,
	-EINVAL,
	-EINVAL,
	-EINVAL,
	-EINVAL,
	-EINVAL,
	-EINVAL,
	-EINVAL,
	-EINVAL,
	-EINVAL,
};
const char *err_msg_expected[17] = {
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
};

int main(int argc, char **argv)
{
	int i;
	int ret;

	params_getopt(argc, argv);

	char kernel_image[4096];
	int os_index = -1;
	char *err_msg = NULL;

	err_msg = malloc(IHKLIB_MAX_SIZE_ERR_MSG);
	INTERR(err_msg == NULL, "allocating err_msg failed\n");

	sprintf(kernel_image, "%s/%s/kernel/mckernel.img",
		QUOTE(WITH_MCK), QUOTE(BUILD_TARGET));

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	for (i = 0; i < 17; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		memset(err_msg, 0, IHKLIB_MAX_SIZE_ERR_MSG);
		ret = ihk_create_os_str(0, &os_index,
					env_str[i], 4,
					kernel_image,
					default_kargs,
					err_msg);

		INFO("err_msg: %s",
		     *err_msg ? err_msg : "(empty)\n");

		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		OKNG(strstr(*err_msg ? err_msg : "(empty)",
			    *err_msg_expected[i] ?
			    err_msg_expected[i] : "(empty)"),
		     "err_msg: %s\texpected: %s\n",
		     *err_msg ? err_msg : "(empty)\n",
		     *err_msg_expected[i] ?
		     err_msg_expected[i] : "(empty)");

		if (ret_expected[i] && ret_expected[i] != -ENOENT) {
			OKNG(ihk_get_num_os_instances(0) == 0,
			     "no os instance created\n");
			OKNG(ihk_get_num_reserved_cpus(0) == 0,
			     "no cpus reserved\n");
			OKNG(ihk_get_num_reserved_mem_chunks(0) == 0,
			     "no memory reserved\n");
		}

		/* Check OS status and clean up*/
		if (ihk_get_num_os_instances(0) > 0) {
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
	if (ihk_get_num_os_instances(0) > 0) {
		ihk_os_shutdown(0);
		os_wait_for_status(IHK_STATUS_INACTIVE);
		cpus_os_release();
		mems_os_release();
		ihk_destroy_os(0, 0);
	}
	cpus_release();
	mems_release();
	linux_rmmod(1);
	free(err_msg);

	return ret;
}
