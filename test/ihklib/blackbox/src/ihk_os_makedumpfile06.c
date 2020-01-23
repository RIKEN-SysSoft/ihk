#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "cpu.h"
#include "mem.h"
#include "os.h"
#include "params.h"
#include "linux.h"

const char param[] = "interactive";
const char *values[] = {
	"0",
	"1",
};

int main(int argc, char **argv)
{
	int ret = 0;
	int opt;
	char *fn = NULL;
	int index = 0;

	params_getopt(argc, argv);

	while ((opt = getopt(argc, argv, "i:f:c")) != -1) {
		switch (opt) {
		case 'i':
			index = atoi(optarg);
			break;
		case 'f':
			fn = optarg;
			break;
		case 'c':
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

			ret = linux_rmmod(0);
			INTERR(ret, "linux_rmmod returned %d\n", ret);

			exit(0);
			break;
		default: /* '?' */
			printf("unknown option %c\n", optopt);
			exit(1);
		}
	}

	int ret_expected[2] = { 0 };
	int ret_access_expected[2] = { 0 };
	int interactive_input[2] = {
		0,
		1,
	};

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	ret = cpus_reserve();
	INTERR(ret, "cpus_reserve returned %d\n", ret);

	struct mems mems = { 0 };
	int excess;

	ret = mems_ls(&mems, "MemFree", 0.9);
	INTERR(ret, "mems_ls returned %d\n", ret);

	excess = mems.num_mem_chunks - 4;
	if (excess > 0) {
		ret = mems_shift(&mems, excess);
		INTERR(ret, "mems_shift returned %d\n", ret);
	}

	mems_fill(&mems, (1UL << 29) / mems.num_mem_chunks);

	ret = ihk_reserve_mem(0, mems.mem_chunks,
			      mems.num_mem_chunks);
	INTERR(ret, "ihk_reserve_mem returned %d\n", ret);

	/* Activate and check */
	START("test-case: %s: %s\n", param, values[index]);

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
	INTERR(ret, "os status didn't change to %d\n",
	       IHK_STATUS_RUNNING);

	ret = ihk_os_makedumpfile(0, fn,
				  24, interactive_input[index]);
	OKNG(ret == ret_expected[index],
	     "return value: %d, expected: %d\n",
	     ret, ret_expected[index]);

	ret = access(fn, F_OK);
	OKNG(ret == ret_access_expected[index],
	     "dumpfile created as expected\n");

	/* Keep OS alive because check is done in script */
	return 0;
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
