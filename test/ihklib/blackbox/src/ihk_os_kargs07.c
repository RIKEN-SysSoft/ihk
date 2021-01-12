#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "cpu.h"
#include "mem.h"
#include "os.h"
#include "params.h"
#include "linux.h"

const char param[] = "os status";
const char *values[] = {
	"missing hidos",
	"first token is empty",
	"last token is empty",
	"middle token is empty",
};
char *kargs[] = {
	"ksyslogd=0",
	" hidos ksyslogd=0",
	"hidos ksyslogd=0 ",
	"hidos  ksyslogd=0",
};
const int ret_expected[] = {
	-EINVAL,
	0,
	0,
	0,
};

int main(int argc, char **argv)
{
	int ret;
	int i;
	int nr_cases = ARRAY_SIZE(values);

	/* Array size check. Don't forget to type commas! */
	ARRAY_SIZE_CHECK(kargs, nr_cases);
	ARRAY_SIZE_CHECK(ret_expected, nr_cases);

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	ret = cpus_reserve();
	INTERR(ret, "cpus_reserve returned %d\n", ret);

	ret = mems_reserve();
	INTERR(ret, "mems_reserve returned %d\n", ret);

	/* Activate and check */
	for (i = 0; i < nr_cases; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_create_os(0);
		INTERR(ret, "ihk_create_os returned %d\n", ret);

		ret = cpus_os_assign();
		INTERR(ret, "cpus_os_assign returned %d\n", ret);

		ret = mems_os_assign();
		INTERR(ret, "mems_os_assign returned %d\n", ret);

		ret = os_load();
		INTERR(ret, "os_load returned %d\n", ret);

		ret = ihk_os_kargs(0, kargs[i]);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		/* Clean up */
		ret = cpus_os_release();
		INTERR(ret, "cpus_os_assign returned %d\n", ret);

		ret = mems_os_release();
		INTERR(ret, "mems_os_assign returned %d\n", ret);

		ret = ihk_destroy_os(0, 0);
		INTERR(ret, "ihk_destroy_os returned %d\n", ret);
	}

	ret = 0;
 out:
	if (ihk_get_num_os_instances(0)) {
		cpus_os_release();
		mems_os_release();
		ihk_destroy_os(0, 0);
	}
	cpus_release();
	mems_release();
	linux_rmmod(1);

	return ret;
}
