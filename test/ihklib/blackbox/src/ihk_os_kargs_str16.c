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

const char param[] = "valid env";
const char *values[] = {
	"IHK_KARGS=hidos allow_oversubscribe ihk_os_kargs_str16",
};

#define NR_CASES (sizeof(values) / sizeof(values[0]))

const char *env_str[] = {
	"IHK_CPUS=12-35\0"
#if NR_NUMA == 1
	"IHK_MEM=1G@0\0"
#elif FIRST_USER_NUMA == 4
	"IHK_MEM=1G@4,512M@5\0"
#else
	"IHK_MEM=1G@0,512M@1\0"
#endif
	"IHK_KARGS=hidos allow_oversubscribe ihk_os_kargs_str16\0"
};

const int ret_expected[] = { 0 };

int main(int argc, char **argv)
{
	int i;
	int ret;

	ARRAY_SIZE_CHECK(values, NR_CASES);
	ARRAY_SIZE_CHECK(env_str, NR_CASES);
	ARRAY_SIZE_CHECK(ret_expected, NR_CASES);

	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	for (i = 0; i < NR_CASES; i++) {
		char *kmsg;
		char *token;

		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_reserve_cpu_str(0, env_str[i], 3);
		INTERR(ret, "ihk_reserve_cpu_str failed with %d\n", ret);

		ret = ihk_reserve_mem_str(0, env_str[i], 3);
		INTERR(ret, "ihk_reserve_mem_str failed with %d\n", ret);

		ret = ihk_create_os(0);
		INTERR(ret < 0, "ihk_reserve_mem_str failed with %d\n", ret);

		ret = ihk_os_assign_cpu_str(0, env_str[i], 3);
		INTERR(ret, "ihk_reserve_mem_str failed with %d\n", ret);

		ret = mems_os_assign();
		INTERR(ret, "mems_os_assign failed with %d\n", ret);

		ret = os_load();
		INTERR(ret, "os_load failed with %d\n", ret);

		ret = ihk_os_kargs_str(0, env_str[i], 3,
				       "hidos allow_oversubscribe");
		INTERR(ret, "ihk_0s_kargs_str failed with %d\n", ret);

		ret = ihk_os_boot(0);
		INTERR(ret, "ihk_os_boot returned %d\n", ret);

		kmsg = calloc(IHK_KMSG_SIZE, sizeof(char));
		INTERR(kmsg == NULL, "allocation of kmsg failed with %d\n", errno);

		ret = ihk_os_kmsg(0, kmsg, IHK_KMSG_SIZE);
		INTERR(ret < 0, "ihk_os_kmsg returned %d\n", ret);

		token = strstr(kmsg, "booted");
		INTERR(token == NULL, "\"booted\" not found in kmsg\n");

		/* let the driver script check the kmsg */
		return 0;
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
	linux_rmmod(0);

	return ret;
}
