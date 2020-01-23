#include <string.h>
#include <limits.h>
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
#include <unistd.h>

const char param[] = "number of pgsizes";
const char *values[] = {
	"INT_MIN",
	"-1",
	"0",
	"1",
	"IHK_MAX_NUM_PGSIZES",
	"IHK_MAX_NUM_PGSIZES + 1",
	"IHK_MAX_NUM_PGSIZES - 1",
	"INT_MAX",
};

int main(int argc, char **argv)
{
	int ret;
	int i;

	params_getopt(argc, argv);

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	ret = cpus_reserve();
	INTERR(ret, "cpus_reserve returned %d\n", ret);

	ret = mems_reserve();
	INTERR(ret, "mems_reserve returned %d\n", ret);

	long pagesizes_input[8][IHK_MAX_NUM_PGSIZES] = { 0 };

	int num_pgsizes_input[] = {
		INT_MIN,
		-1,
		0,
		1,
		IHK_MAX_NUM_PGSIZES,
		IHK_MAX_NUM_PGSIZES + 1,
		IHK_MAX_NUM_PGSIZES - 1,
		INT_MAX
	};

	int ret_expected[] = {
		-EINVAL,
		-EINVAL,
		-EINVAL,
		-EINVAL,
		0,
		-EINVAL,
		-EINVAL,
		-EINVAL,
	};

	long pagesizes_expected[8][IHK_MAX_NUM_PGSIZES] = {
		{ 0 },
		{ 0 },
		{ 0 },
		{ 0 },
		{
		1UL << 12,
		1UL << 16,
		1UL << 21,
		1UL << 25,
		1UL << 30,
		1UL << 34,
		1UL << 29,
		1UL << 42,
		},
		{ 0 },
		{ 0 },
		{ 0 },
	};

	for (i = 0; i < 8; i++) {
		START("test-case: %s: %s\n", param, values[i]);

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

		ret = ihk_os_get_pagesizes(0, pagesizes_input[i],
				num_pgsizes_input[i]);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		ret = memcmp(pagesizes_input[i], pagesizes_expected[i],
			     IHK_MAX_NUM_PGSIZES * sizeof(long));
		OKNG(ret == 0, "get pagesizes as expected\n");

		ret = ihk_os_shutdown(0);
		INTERR(ret, "ihk_os_shutdown returned %d\n", ret);

		ret = os_wait_for_status(IHK_STATUS_INACTIVE);
		INTERR(ret, "os status didn't change to %d\n",
		       IHK_STATUS_INACTIVE);

		ret = mems_os_release();
		INTERR(ret, "mems_os_release returned %d\n", ret);

		ret = cpus_os_release();
		INTERR(ret, "cpus_os_release returned %d\n", ret);

		ret = ihk_destroy_os(0, 0);
		INTERR(ret, "ihk_destroy_os returned %d\n", ret);
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
	linux_rmmod(0);

	return ret;
}

