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
const char *values[] = {
	"non-root"
};

int main(int argc, char **argv)
{
	int ret;
	int i;
	int num_assigned_cpu = 0;

	params_getopt(argc, argv);

	int ret_expected[] = { -EPERM };
	struct ikc_cpu_map map_input[1] = { 0 };

	struct cpus cpus_linux = { 0 };
	struct cpus cpus_mckernel = { 0 };
	struct ikc_cpu_map map_after_set[1] = { 0 };
	struct ikc_cpu_map *map_expected[] = {
		&map_after_set[0],
	};

	/* Parse additional options */
	int opt;

	while ((opt = getopt(argc, argv, "irn:")) != -1) {
		switch (opt) {
		case 'i':
			/* Precondition */
			ret = linux_insmod(1);
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

			ret = ihk_os_get_num_assigned_cpus(0);
			INTERR(ret < 0,
			"ihk_os_get_num_assigned_cpus returned %d\n", ret);
			num_assigned_cpu = ret;

			return num_assigned_cpu;
		case 'n':
			num_assigned_cpu = atoi(optarg);
			break;

		case 'r':
			ret = os_load();
			INTERR(ret, "os_load returned %d\n", ret);

			ret = os_kargs();
			INTERR(ret, "os_kargs returned %d\n", ret);

			ret = ihk_os_boot(0);
			INTERR(ret, "ihk_os_boot returned %d\n", ret);

			if (map_expected[0]) {
				/* ikc map when ihk_os_set_ikc_map isn't
				 * called
				 */
				int j;
				int ncpus_assigned;

				ret = cpus_ls(&cpus_linux);
				INTERR(ret, "cpus_ls returned %d\n",
				       ret);

				ret = ihk_os_get_num_assigned_cpus(0);
				INTERR(ret <= 0,
				       "ihk_os_get_num_assigned_cpus returned %d\n",
				       ret);
				INFO("# of assigned cpus: %d\n", ret);
				ncpus_assigned = ret;
				
				ret = cpus_init(&cpus_mckernel, ncpus_assigned);
				INTERR(ret, "cpus_init returned %d\n", ret);
					
				ret = ihk_os_query_cpu(0,
						       cpus_mckernel.cpus,
						       cpus_mckernel.ncpus);
				INTERR(ret < 0, "ihk_os_query_cpu returned %d\n",
				       ret);

				ret = ikc_cpu_map_init(&map_after_set[0],
						       cpus_mckernel.ncpus);
				INTERR(ret, "ikc_cpu_map_init "
				       "returned %d\n", ret);

				for (j = 0; j < map_after_set[0].ncpus; j++) {
					map_after_set[0].map[j].src_cpu =
						cpus_mckernel.cpus[j];
					map_after_set[0].map[j].dst_cpu =
						cpus_linux.cpus[0];
				}

				ret = ikc_cpu_map_check(map_expected[0]);
				OKNG(ret == 0,
				     "ikc map configured as expected\n");
			}

			/* Clean up */
			ret = ihk_os_shutdown(0);
			INTERR(ret, "ihk_os_shutdown returned %d\n", ret);

			ret = os_wait_for_status(IHK_STATUS_INACTIVE);
			INTERR(ret, "os_wait_for_status returned %d\n", ret);

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
		default: /* '?' */
			printf("unknown option %c\n", optopt);
			exit(1);
		}
	}

	/* Activate and check */
	for (i = 0; i < 1; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		ret = ikc_cpu_map_init(&map_input[i], num_assigned_cpu);
		INTERR(ret, "ikc_cpu_map_init returned %d\n", ret);

		ret = linux_wait_chmod(0);
		INTERR(ret, "device file mode didn't change to 0666\n");

		ret = ihk_os_set_ikc_map(0, map_input[i].map,
				map_input[i].ncpus);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);
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
		mems_release();
		cpus_release();
		linux_rmmod(0);
	}
	return ret;
}
