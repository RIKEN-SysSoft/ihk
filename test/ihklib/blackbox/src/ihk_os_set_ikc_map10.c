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

const char param[] = "IKC destination CPUs";
const char *values[] = {
	"all Linux CPUs - 1",
	"all Linux CPUs",
	"include one McKernel CPU",
};

int ikc_cpu_map_dst_max_min(struct ikc_cpu_map *map, int *dst_max, int *dst_min)
{
	int i;
	int ret = 0;
	int _dst_min, _dst_max;

	if (map == NULL || map->map == NULL) {
		fprintf(stderr, "%s: Invalid map\n", __func__);
		ret = -EINVAL;
		goto out;
	}

	_dst_min = _dst_max = map->map[0].dst_cpu;

	for (i = 1; i < map->ncpus; i++) {
		int dst_cpu = map->map[i].dst_cpu;

		if (dst_cpu > _dst_max) {
			_dst_max = dst_cpu;
		}
		else if (dst_cpu < _dst_min) {
			_dst_min = dst_cpu;
		}
	}

	if (dst_max) {
		*dst_max = _dst_max;
	}
	if (dst_min) {
		*dst_min = _dst_min;
	}

	ret = 0;
out:
	return ret;
}

int ikc_cpu_map_dst_overwrite(struct ikc_cpu_map *map, int old_dst, int new_dst)
{
	int i;
	int ret = 0;

	if (map == NULL || map->map == NULL) {
		fprintf(stderr, "%s: Invalid map\n", __func__);
		ret = -EINVAL;
		goto out;
	}

	for (i = 0; i < map->ncpus; i++) {
		int dst_cpu = map->map[i].dst_cpu;

		if (dst_cpu == old_dst) {
			map->map[i].dst_cpu = new_dst;
		}
	}

	ret = 0;
out:
	return ret;
}

int main(int argc, char **argv)
{
	int ret;
	int i;

	params_getopt(argc, argv);

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	int j;

	struct cpus cpus_mckernel = { 0 };
	struct cpus cpus_linux = { 0 };

	ret = cpus_ls(&cpus_mckernel);
	INTERR(ret, "cpus_ls returned %d\n", ret);

	ret = cpus_shift(&cpus_mckernel, 2);
	INTERR(ret, "cpus_shift returned %d\n", ret);

	ret = cpus_ls(&cpus_linux);
	INTERR(ret, "cpus_ls returned %d\n", ret);

	ret = cpus_pop(&cpus_linux, cpus_linux.ncpus - 2);
	INTERR(ret, "cpus_pop returned %d\n", ret);

	/* Precondition */
	ret = cpus_reserve();
	INTERR(ret, "cpus_reserve returned %d\n", ret);

	ret = mems_reserve();
	INTERR(ret, "mems_reserve returned %d\n", ret);

	/* Prepare one with NULL and zero-clear others */
	struct ikc_cpu_map map_input[3] = { 0 };
	struct ikc_cpu_map map_after_set[3] = { 0 };

	for (i = 0; i < 3; i++) {
		int dst_max, dst_min;
		int new_dst;

		ret = ikc_cpu_map_2toN(&map_input[i]);
		INTERR(ret, "ikc_cpu_map_2toN returned %d\n", ret);

		ret = ikc_cpu_map_2toN(&map_after_set[i]);
		INTERR(ret, "ikc_cpu_map_2toN returned %d\n", ret);

		ret = ikc_cpu_map_dst_max_min(&map_input[i],
				&dst_max, &dst_min);
		INTERR(ret, "ikc_cpu_map_dst_max_min returned %d\n", ret);

		switch (i) {
		case 0:
			ret = ikc_cpu_map_dst_overwrite(&map_input[i],
					dst_max, dst_min);
			INTERR(ret, "ikc_cpu_map_dst_overwrite returned %d\n",
				ret);

			ret = ikc_cpu_map_dst_overwrite(&map_after_set[i],
					dst_max, dst_min);
			INTERR(ret, "ikc_cpu_map_dst_overwrite returned %d\n",
				ret);
			break;
		case 2:
			ikc_cpu_map_max_src_cpu(&map_input[i], &new_dst, NULL);
			ret = ikc_cpu_map_dst_overwrite(&map_input[i],
				dst_max, new_dst);

			/* ikc map when ihk_os_set_ikc_map isn't called */
			ret = ikc_cpu_map_init(&map_after_set[i],
					       cpus_mckernel.ncpus);
			INTERR(ret, "ikc_cpu_map_init returned %d\n", ret);

			for (j = 0; j < cpus_mckernel.ncpus; j++) {
				map_after_set[i].map[j].src_cpu =
					cpus_mckernel.cpus[j];
				map_after_set[i].map[j].dst_cpu =
					cpus_linux.cpus[0];
			}
			break;
		default:
			break;
		}
	}

	int ret_expected[3] = {
		0,
		0,
		-EINVAL,
	};

	struct ikc_cpu_map *map_expected[] = {
		&map_after_set[0],
		&map_after_set[1],
		&map_after_set[2],
	};


	/* Activate and check */
	for (i = 0; i < 3; i++) {
		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_create_os(0);
		INTERR(ret, "ihk_create_os returned %d\n", ret);

		ret = cpus_os_assign();
		INTERR(ret, "cpus_os_assign returned %d\n", ret);

		ret = mems_os_assign();
		INTERR(ret, "mems_os_assign returned %d\n", ret);

		ret = ihk_os_set_ikc_map(0, map_input[i].map,
				map_input[i].ncpus);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		ret = os_load();
		INTERR(ret, "os_load returned %d\n", ret);

		ret = os_kargs();
		INTERR(ret, "os_kargs returned %d\n", ret);

		ret = ihk_os_boot(0);
		INTERR(ret, "ihk_os_boot returned %d\n", ret);

		if (map_expected[i]) {
			ret = ikc_cpu_map_check_channels(map_input[i].ncpus);
			OKNG(ret == 0, "IKCs from all cpus succeeded\n");

			ret = ikc_cpu_map_check(map_expected[i]);
			OKNG(ret == 0, "ikc map configured as expected\n");
		}

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

	ret = 0;
 out:
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

	return ret;
}

