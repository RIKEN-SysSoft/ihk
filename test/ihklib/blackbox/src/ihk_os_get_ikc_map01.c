#include <errno.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "cpu.h"
#include "mem.h"
#include "os.h"
#include "params.h"
#include "linux.h"

const char param[] = "existence of os instance";
const char *values[] = {
	"before ihk_create_os()",
	"after ihk_create_os()",
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

	int num_cpus = ihk_get_num_reserved_cpus(0);

	INTERR(num_cpus < 0, "mems_reserve returned %d\n", ret);

	struct ikc_cpu_map map_input[2] = { 0 };
	struct ikc_cpu_map map_input_set[2] = { 0 };
	struct ikc_cpu_map map_after_get[2] = { 0 };

	for (i = 0; i < 2; i++) {
		ret = ikc_cpu_map_2toN(&map_input_set[i]);
		INTERR(ret, "ikc_cpu_map_2toN returned %d\n", ret);

		ret = ikc_cpu_map_init(&map_input[i], num_cpus);
		INTERR(ret, "ikc_cpu_map_init returned %d\n", ret);
	}

	ret = ikc_cpu_map_2toN(&map_after_get[1]);
	INTERR(ret, "ikc_cpu_map_2toN returned %d\n", ret);

	int ret_expected[] = {
		-ENOENT,
		0
	};

	struct ikc_cpu_map *map_expected[] = {
		NULL,
		&map_after_get[1]
	};

	/* Activate and check */
	for (i = 0; i < 2; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		if (i == 1) {
			ret = ihk_create_os(0);
			INTERR(ret, "ihk_create_os returned %d\n", ret);

			ret = cpus_os_assign();
			INTERR(ret, "cpus_os_assign returned %d\n", ret);

			ret = mems_os_assign();
			INTERR(ret, "mems_os_assign returned %d\n", ret);

			ret = ihk_os_set_ikc_map(0, map_input_set[i].map,
				map_input_set[i].ncpus);
			INTERR(ret, "ihk_os_set_ikc_map returned %d\n", ret);

			ret = os_load();
			INTERR(ret, "os_load returned %d\n", ret);

			ret = os_kargs();
			INTERR(ret, "os_kargs returned %d\n", ret);

			ret = ihk_os_boot(0);
			INTERR(ret, "ihk_os_boot returned %d\n", ret);
		}

		ret = ihk_os_get_ikc_map(0, map_input[i].map,
					 map_input[i].ncpus);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		if (map_expected[i]) {
			ret = ikc_cpu_map_compare(&map_input[i],
						  map_expected[i]);
			OKNG(ret == 0, "map got matches map set\n");
		}

		if (i == 1) {
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
	mems_release();
	cpus_release();
	linux_rmmod(0);

	return ret;
}
