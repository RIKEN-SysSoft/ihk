#define _GNU_SOURCE	 /* See feature_test_macros(7) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <sys/mman.h>
#include <ihklib.h>
#include "util.h"
#include "okng.h"
#include "cpu.h"


int cpus_init(struct cpus *cpus, int ncpus)
{
	int ret;

	if (ncpus == 0) {
		ret = 0;
		goto out;
	}

	cpus->cpus = mmap(0, sizeof(int) * ncpus,
			  PROT_READ | PROT_WRITE,
			  MAP_ANONYMOUS | MAP_PRIVATE,
			  -1, 0);
	if (cpus->cpus == MAP_FAILED) {
		ret = -errno;
		goto out;
	}

	cpus->ncpus = ncpus;
	ret = 0;
 out:
	return ret;
}

int cpus_copy(struct cpus *dst, struct cpus *src)
{
	int ret;

	if (dst->cpus) {
		dst->cpus = mremap(dst->cpus,
				   sizeof(int) * dst->ncpus,
				   sizeof(int) * src->ncpus,
				   MREMAP_MAYMOVE);
		dst->ncpus = src->ncpus;
	} else {
		ret = cpus_init(dst, src->ncpus);
		if (ret) {
			goto out;
		}
	}

	memcpy(dst->cpus, src->cpus, sizeof(int) * src->ncpus);

	ret = 0;
 out:
	return ret;
}

int _cpus_ls(struct cpus *cpus, char *online)
{
	char cmd[1024];
	FILE *fp;
	int ncpus;
	int ret;

	sprintf(cmd, "lscpu -p=cpu --%s | awk '!/#/ { print $0; }'", online);
	//INFO("%s\n", cmd);
	fp = popen(cmd, "r");
	if (fp == NULL) {
		ret = -errno;
		goto out;
	}

	if (cpus->cpus == NULL) {
		ret = cpus_init(cpus, MAX_NUM_CPUS);
		if (ret != 0) {
			goto out;
		}
	}

	ncpus = 0;
	do {
		int id;

		ret = fscanf(fp, "%d", &id);
		if (ret == -1)
			break;

		if (ret != 1)
			continue;

		cpus->cpus[ncpus++] = id;
	} while (ret);

	cpus->cpus = mremap(cpus->cpus, sizeof(int) * cpus->ncpus,
			    sizeof(int) * ncpus, MREMAP_MAYMOVE);
	if (cpus->cpus == MAP_FAILED) {
		ret = -errno;
		goto out;
	}

	cpus->ncpus = ncpus;

	ret = 0;
 out:
	if (fp) {
		pclose(fp);
	}
	return ret;
}

int cpus_ls(struct cpus *cpus)
{
	return _cpus_ls(cpus, "online");
}

int cpus_max_id(struct cpus *cpus)
{
	int i;
	int max = INT_MIN;

	for (i = 0; i < cpus->ncpus; i++) {
		if (cpus->cpus[i] > max) {
			max = cpus->cpus[i];
		}
	}

	return max;
}

int cpus_push(struct cpus *cpus, int id)
{
	int ret;

	if (cpus->cpus == NULL) {
		ret = cpus_init(cpus, 1);
		if (ret != 0) {
			goto out;
		}
		cpus->ncpus = 0;
	} else {
		cpus->cpus = mremap(cpus->cpus, sizeof(int) * cpus->ncpus,
				    sizeof(int) * (cpus->ncpus + 1),
				    MREMAP_MAYMOVE);
		if (cpus->cpus == MAP_FAILED) {
			ret = -errno;
			goto out;
		}
	}

	cpus->cpus[cpus->ncpus] = id;
	cpus->ncpus++;

	ret = 0;
 out:
	return ret;
}

int cpus_pop(struct cpus *cpus, int n)
{
	int ret;

	if (n == 0) {
		ret = 0;
		goto out;
	}

	if (cpus->ncpus < n || cpus->cpus == NULL) {
		ret = 1;
		goto out;
	}

	if (cpus->ncpus == n) {
		ret = munmap(cpus->cpus, sizeof(int) * cpus->ncpus);
		if (ret) {
			ret = -errno;
			goto out;
		}
		cpus->cpus = NULL;
		cpus->ncpus = 0;
		ret = 0;
		goto out;
	}

	cpus->cpus = mremap(cpus->cpus, sizeof(int) * cpus->ncpus,
			    sizeof(int) * (cpus->ncpus - n),
			    MREMAP_MAYMOVE);
	if (cpus->cpus == MAP_FAILED) {
		ret = -errno;
		goto out;
	}

	cpus->ncpus -= n;

	ret = 0;
 out:
	return ret;
}

int cpus_shift(struct cpus *cpus, int n)
{
	int ret;

	if (n == 0) {
		ret = 0;
		goto out;
	}

	if (cpus->ncpus < n || cpus->cpus == NULL) {
		ret = -EINVAL;
		goto out;
	}

	if (cpus->ncpus == n) {
		ret = munmap(cpus->cpus, sizeof(int) * cpus->ncpus);
		if (ret) {
			ret = -errno;
			goto out;
		}
		cpus->cpus = NULL;
		cpus->ncpus = 0;
		ret = 0;
		goto out;
	}

	memmove(cpus->cpus, cpus->cpus + n, sizeof(int) * (cpus->ncpus - n));

	cpus->cpus = mremap(cpus->cpus, sizeof(int) * cpus->ncpus,
			    sizeof(int) * (cpus->ncpus - n),
			    MREMAP_MAYMOVE);
	if (cpus->cpus == MAP_FAILED) {
		ret = -errno;
		goto out;
	}

	cpus->ncpus -= n;

	ret = 0;
 out:
	return ret;
}

void cpus_fill(struct cpus *cpus, int id)
{
	int i;

	for (i = 0; i < cpus->ncpus; i++) {
		cpus->cpus[i] = id;
	}
}


void cpus_dump(struct cpus *cpus)
{
	int i;

	INFO("ncpus: %d\n", cpus->ncpus);

	if (cpus->cpus == NULL) {
		INFO("cpus->cpus is NULL\n");
		return;
	}

	for (i = 0; i < cpus->ncpus; i++) {
		INFO("cpus[%d]: %d\n", i, cpus->cpus[i]);
	}
}

int cpus_compare(struct cpus *result, struct cpus *expected)
{
	int i;

	if (result == NULL && expected == NULL) {
		return 0;
	}

	if (result->ncpus != expected->ncpus) {
		return 1;
	}

	for (i = 0; i < result->ncpus; i++) {
		if (result->cpus[i] != expected->cpus[i]) {
			return 1;
		}
	}
	return 0;
}

int cpus_check_reserved(struct cpus *expected)
{
	int ret;
	struct cpus cpus = { 0 };

	ret = ihk_get_num_reserved_cpus(0);
	INTERR(ret < 0, "ihk_get_num_reserved_cpus returned %d\n",
	       ret);
	INFO("# of reserved cpus: %d\n", ret);

	if (ret > 0) {
		ret = cpus_init(&cpus, ret);
		INTERR(ret, "cpus_init returned %d\n", ret);

		ret = ihk_query_cpu(0, cpus.cpus, cpus.ncpus);
		INTERR(ret < 0, "ihk_query_cpu returned %d\n",
		       ret);
	}

	ret = cpus_compare(&cpus, expected);
	if (ret) {
		INFO("actual reservation:\n");
		cpus_dump(&cpus);
		INFO("expected reservation:\n");
		cpus_dump(expected);
	}

 out:
	return ret;
}

int cpus_reserved(struct cpus *cpus)
{
	int ret;
	int num_cpus;

	ret = ihk_get_num_reserved_cpus(0);
	INTERR(ret < 0, "ihk_get_num_reserved_cpus returned %d\n",
	       ret);
	num_cpus = ret;

	if (num_cpus > 0) {
		ret = cpus_init(cpus, num_cpus);
		INTERR(ret, "cpus_init returned %d\n", ret);

		ret = ihk_query_cpu(0, cpus->cpus, cpus->ncpus);
		INTERR(ret < 0, "ihk_query_cpu returned %d\n",
		       ret);
	}

	ret = 0;
 out:
	return ret;
}

int cpus_check_assigned(struct cpus *expected)
{
	int ret;
	struct cpus cpus = { 0 };

	ret = ihk_os_get_num_assigned_cpus(0);
	INTERR(ret < 0, "ihk_os_get_num_assigned_cpus returned %d\n",
	       ret);
	INFO("# of assigned cpus: %d\n", ret);

	if (ret > 0) {
		ret = cpus_init(&cpus, ret);
		INTERR(ret, "cpus_init returned %d\n", ret);

		ret = ihk_os_query_cpu(0, cpus.cpus, cpus.ncpus);
		INTERR(ret < 0, "ihk_os_query_cpu returned %d\n",
		       ret);
	}

	ret = cpus_compare(&cpus, expected);
	if (ret) {
		INFO("actual reservation:\n");
		cpus_dump(&cpus);
		INFO("expected reservation:\n");
		cpus_dump(expected);
	}

 out:
	return ret;
}

int _cpus_reserve(int nlinux, int nmck)
{
	int ret;
	struct cpus cpus = { 0 };

	ret = cpus_ls(&cpus);
	INTERR(ret, "cpus_ls returned %d\n", ret);

	ret = cpus_shift(&cpus, nlinux);
	INTERR(ret, "cpus_shift returned %d\n", ret);

	if (nmck != -1) {
		ret = cpus_pop(&cpus, cpus.ncpus - nmck);
		INTERR(ret, "cpus_pop returned %d\n", ret);
	}

	ret = ihk_reserve_cpu(0, cpus.cpus, cpus.ncpus);
	INTERR(ret, "ihk_reserve_cpu returned %d\n", ret);

	ret = 0;
 out:
	return ret;
}

int cpus_reserve(void)
{
	return _cpus_reserve(2, -1);
}

int cpus_release(void)
{
	int ret;
	struct cpus cpus = { 0 };

	ret = ihk_get_num_reserved_cpus(0);
	INTERR(ret < 0, "ihk_get_num_reserved_cpus returned %d\n",
	       ret);

	if (ret == 0) {
		goto out;
	}

	ret = cpus_init(&cpus, ret);
	INTERR(ret, "cpus_init returned %d\n", ret);

	ret = ihk_query_cpu(0, cpus.cpus, cpus.ncpus);
	INTERR(ret, "ihk_query_cpu returned %d\n",
	       ret);

	ret = ihk_release_cpu(0, cpus.cpus, cpus.ncpus);
	INTERR(ret, "ihk_release_cpu returned %d\n", ret);

	ret = 0;
 out:
	return ret;
}

int cpus_os_assign(void)
{
	int ret;
	struct cpus cpus = {0};

	ret = cpus_reserved(&cpus);
	INTERR(ret, "cpus_reserved returned %d\n", ret);

	ret = ihk_os_assign_cpu(0, cpus.cpus, cpus.ncpus);
	INTERR(ret, "ihk_os_assign_cpu returned %d\n", ret);

	ret = 0;
out:
	return ret;
}

int cpus_os_release(void)
{
	int ret;
	struct cpus cpus = {0};

	ret = ihk_os_get_num_assigned_cpus(0);
	INTERR(ret < 0, "ihk_os_get_num_assigned_cpus returned %d\n",
	       ret);

	if (ret > 0) {
		ret = cpus_init(&cpus, ret);
		INTERR(ret, "cpus_init returned %d\n", ret);

		ret = ihk_os_query_cpu(0, cpus.cpus, cpus.ncpus);
		INTERR(ret, "ihk_os_query_cpu returned %d\n",
		       ret);

		ret = ihk_os_release_cpu(0, cpus.cpus, cpus.ncpus);
		INTERR(ret, "ihk_os_release_cpu returned %d\n",
		       ret);
	}

	ret = 0;
out:
	return ret;
}

int cpus_at(struct cpus *cpus, int index)
{
	int ret;

	ret = cpus_pop(cpus, cpus->ncpus - (index + 1));
	INTERR(ret, "cpus_pop returned %d\n", ret);

	ret = cpus_shift(cpus, index);
	INTERR(ret, "cpus_shift returned %d\n", ret);

 out:
	return ret;
}

int cpus_broadcast(struct cpus *cpus, int ncpus)
{
	int ret;
	int i;

	for (i = 0; i < ncpus - 1; i++) {
		ret = cpus_push(cpus, cpus->cpus[0]);
		INTERR(ret, "cpus_push returned %d\n", ret);
	}
 out:
	return ret;
}

int cpus_toggle(int cpu_id, char *toggle)
{
	int ret;
	int on_off = 0;
	char cmd[1024];

	if (cpu_id < 0) {
		printf("%s: invalid cpu_id (%d)\n", __func__, cpu_id);
		ret = -EINVAL;
		goto out;
	}

	if (!strcasecmp(toggle, "on")) {
		on_off = 1;
	}
	else if (!strcasecmp(toggle, "off")) {
		on_off = 0;
	}
	else {
		printf("%s: invalid argument (%s)\n", __func__, toggle);
		ret = -EINVAL;
		goto out;
	}

	sprintf(cmd, "echo %d > /sys/devices/system/cpu/cpu%d/online",
		on_off, cpu_id);

	ret = system(cmd);
	ret = WEXITSTATUS(ret);
	INTERR(ret, "%s returned %d\n", cmd, ret);

	ret = 0;
out:
	return ret;
}

int ikc_cpu_map_init(struct ikc_cpu_map *map, int ncpus)
{
	int ret;

	if (ncpus == 0) {
		ret = 0;
		goto out;
	}

	map->map = mmap(0, sizeof(struct ihk_ikc_cpu_map) * ncpus,
			PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE,
			-1, 0);
	if (map->map == MAP_FAILED) {
		ret = -errno;
		goto out;
	}

	map->ncpus = ncpus;
	ret = 0;
 out:
	return ret;
}

/* structure of arrays to array of structures */
int ikc_cpu_map_copy(struct ikc_cpu_map *map, struct cpus *src_cpus,
		     struct cpus *dst_cpus)
{
	int ret;
	int i;

	if (src_cpus->ncpus != dst_cpus->ncpus) {
		ret = -EINVAL;
		goto out;
	}

	if (src_cpus->ncpus == 0) {
		ret = 0;
		goto out;
	}

	ret = ikc_cpu_map_init(map, src_cpus->ncpus);
	INTERR(ret, "ikc_cpu_map_init returned %d\n", ret);

	for (i = 0; i < src_cpus->ncpus; i++) {
		map->map[i].src_cpu = src_cpus->cpus[i];
		map->map[i].dst_cpu = dst_cpus->cpus[i];
	}
 out:
	return ret;
}

/* a = b + c */
int ikc_cpu_map_cat(struct ikc_cpu_map *a, struct ikc_cpu_map *b,
		    struct ikc_cpu_map *c)
{
	int ret;

	if (b->ncpus + c->ncpus == 0) {
		ret = 0;
		goto out;
	}

	ret = ikc_cpu_map_init(a, b->ncpus + c->ncpus);
	INTERR(ret, "ikc_cpu_map_init returned %d\n", ret);

	memcpy(a->map, b->map,
	       sizeof(struct ihk_ikc_cpu_map) * b->ncpus);
	memcpy(a->map + b->ncpus, c->map,
	       sizeof(struct ihk_ikc_cpu_map) * c->ncpus);

	a->ncpus = b->ncpus + c->ncpus;

	ret = 0;
 out:
	return ret;
}

void ikc_cpu_map_dump(struct ikc_cpu_map *map)
{
	int i;

	for (i = 0; i < map->ncpus; i++) {
		INFO("index: %d, src_cpu: %d, dst_cpu: %d\n",
		     i, map->map[i].src_cpu, map->map[i].dst_cpu);
	}
}

int ikc_cpu_map_compare(struct ikc_cpu_map *result,
			struct ikc_cpu_map *expected)
{
	int i, j;

	if (result == NULL && expected == NULL) {
		return 0;
	}

	if (result->ncpus != expected->ncpus) {
		return 1;
	}

	for (i = 0; i < result->ncpus; i++) {
		for (j = 0; j < expected->ncpus; j++) {
			if (result->map[i].src_cpu !=
			    expected->map[j].src_cpu) {
				continue;
			}
			if (result->map[i].dst_cpu !=
			    expected->map[j].dst_cpu) {
				return 1;
			}
		}
	}
	return 0;
}

int ikc_cpu_map_check(struct ikc_cpu_map *expected)
{
	int ret;
	int ncpus;
	struct ikc_cpu_map map = { 0 };

	ret = ihk_os_get_num_assigned_cpus(0);
	INTERR(ret < 0, "ihk_os_get_num_assigned_cpus returned %d\n",
	       ret);
	ncpus = ret;

	if (ncpus > 0) {
		ret = ikc_cpu_map_init(&map, ncpus);
		INTERR(ret, "ikc_cpu_map_init returned %d\n", ret);

		ret = ihk_os_get_ikc_map(0, map.map, map.ncpus);
		INTERR(ret < 0, "ihk_os_get_ikc_map returned %d\n",
		       ret);
	}

	ret = ikc_cpu_map_compare(&map, expected);
	if (ret) {
		INFO("actual map:\n");
		ikc_cpu_map_dump(&map);
		INFO("expected map:\n");
		ikc_cpu_map_dump(expected);
	}

 out:
	return ret;
}

/* -r 2-(N/2):0+(N/2+1)-(N-1):1*/
int ikc_cpu_map_2toN(struct ikc_cpu_map *map)
{
	int ret;
	struct cpus cpus_lwk_1st = { 0 };
	struct cpus cpus_lwk_2nd = { 0 };

	/* first half */
	ret = cpus_reserved(&cpus_lwk_1st);
	INTERR(ret, "cpus_reserved returned %d\n", ret);
	ret = cpus_pop(&cpus_lwk_1st,
		       cpus_lwk_1st.ncpus / 2);
	INTERR(ret, "cpus_pop returned %d\n", ret);

	/* others */
	ret = cpus_reserved(&cpus_lwk_2nd);
	INTERR(ret, "cpus_reserved returned %d\n", ret);
	ret = cpus_shift(&cpus_lwk_2nd,
			 cpus_lwk_1st.ncpus);
	INTERR(ret, "cpus_shift returned %d\n", ret);

	struct cpus cpus_linux_1st = { 0 };
	struct cpus cpus_linux_2nd = { 0 };

	/* array of first cpu */
	ret = cpus_ls(&cpus_linux_1st);
	INTERR(ret, "cpus_reserved returned %d\n", ret);
	ret = cpus_at(&cpus_linux_1st, 0);
	INTERR(ret, "cpus_at returned %d\n", ret);
	ret = cpus_broadcast(&cpus_linux_1st, cpus_lwk_1st.ncpus);
	INTERR(ret, "cpus_broadcast returned %d\n", ret);

	/* array of second cpu */
	ret = cpus_ls(&cpus_linux_2nd);
	INTERR(ret, "cpus_reserved returned %d\n", ret);
	ret = cpus_at(&cpus_linux_2nd, 1);
	INTERR(ret, "cpus_at returned %d\n", ret);
	ret = cpus_broadcast(&cpus_linux_2nd, cpus_lwk_2nd.ncpus);
	INTERR(ret, "cpus_broadcast returned %d\n", ret);

	/* transform into array of <ikc_src, ikc_dst> */
	struct ikc_cpu_map map_1st = { 0 };
	struct ikc_cpu_map map_2nd = { 0 };

	ret = ikc_cpu_map_copy(&map_1st, &cpus_lwk_1st, &cpus_linux_1st);
	INTERR(ret, "ihc_cpu_map_copy returned %d\n", ret);

	ret = ikc_cpu_map_copy(&map_2nd, &cpus_lwk_2nd, &cpus_linux_2nd);
	INTERR(ret, "ihc_cpu_map_copy returned %d\n", ret);

	/* cat 1st and 2nd */
	ret = ikc_cpu_map_cat(map, &map_1st, &map_2nd);
	INTERR(ret, "ihc_cpu_map_cat returned %d\n", ret);
	//ikc_cpu_map_dump(map);

	ret = 0;
 out:
	return ret;
}

void ikc_cpu_map_max_src_cpu(struct ikc_cpu_map *map, int *src_cpu,
			    int *dst_cpu)
{
	int i;
	int src = INT_MIN, dst = 0;

	for (i = 0; i < map->ncpus; i++) {
		if (map->map[i].src_cpu > src) {
			src = map->map[i].src_cpu;
			dst = map->map[i].dst_cpu;
		}
	}

	if (src_cpu) {
		*src_cpu = src;
	}
	if (dst_cpu) {
		*dst_cpu = dst;
	}
}

int ikc_cpu_map_push(struct ikc_cpu_map *map, int src_cpu, int dst_cpu)
{
	int ret;

	if (map->map == NULL) {
		ret = ikc_cpu_map_init(map, 1);
		if (ret) {
			goto out;
		}
		map->ncpus = 0;
	} else {
		map->map = mremap(map->map,
				  sizeof(struct ihk_ikc_cpu_map) * map->ncpus,
				  sizeof(struct ihk_ikc_cpu_map) *
				  (map->ncpus + 1),
				  MREMAP_MAYMOVE);
		if (map->map == MAP_FAILED) {
			ret = -errno;
			goto out;
		}
	}

	map->map[map->ncpus].src_cpu = src_cpu;
	map->map[map->ncpus].dst_cpu = dst_cpu;
	map->ncpus++;

	ret = 0;
 out:
	return ret;
}

int ikc_cpu_map_pop(struct ikc_cpu_map *map, int n)
{
	int ret;

	if (map->ncpus < n || map->map == NULL) {
		ret = 1;
		goto out;
	}

	if (map->ncpus == n) {
		ret = munmap(map->map,
			     sizeof(struct ihk_ikc_cpu_map) * map->ncpus);
		if (ret) {
			ret = -errno;
			goto out;
		}
		map->map = NULL;
		map->ncpus = 0;
		ret = 0;
		goto out;
	}

	map->map = mremap(map->map,
			  sizeof(struct ihk_ikc_cpu_map) * map->ncpus,
			  sizeof(struct ihk_ikc_cpu_map) * (map->ncpus - n),
			  MREMAP_MAYMOVE);
	if (map->map == MAP_FAILED) {
		ret = -errno;
		goto out;
	}

	map->ncpus -= n;

	ret = 0;
 out:
	return ret;
}

int ikc_cpu_map_check_channels(int nchannels)
{
	int ret;
	FILE *fp = NULL;
	int ncpus;
	char cmd[4096];

	sprintf(cmd, "%s/bin/mcexec %s/bin/ikc_map.sh %d |"
		" sort | uniq | wc -l",
		QUOTE(WITH_MCK), QUOTE(CMAKE_INSTALL_PREFIX),
		nchannels);
	fp = popen(cmd, "r");

	if (fp == NULL) {
		int errno_save = errno;

		printf("%s: popen returned %d\n",
		       __func__, errno);
		ret = -errno_save;
		goto out;
	}

	ret = fscanf(fp, "%d\n", &ncpus);
	INFO("# of tokens: %d, ncpus: %d, nchannels: %d\n",
	     ret, ncpus, nchannels);
	ret = (ret == 1 && ncpus == nchannels) ? 0 : 1;

 out:
	if (fp) {
		pclose(fp);
	}
	return ret;
}
