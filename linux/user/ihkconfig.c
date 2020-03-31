/**
 * \file ihkconfig.c
 *  License details are found in the file LICENSE.
 * \brief
 *  configures the IHK device
 * \author Taku Shimosawa  <shimosawa@is.s.u-tokyo.ac.jp> \par
 * \author Balazs Gerofi  <bgerofi@riken.jp> \par
 * Copyright (C) 2011-2017 RIKEN AICS>
 */
#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include "ihk/ihk_host_user.h"
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <ihk/ihklib.h>
#include <ihk/ihklib_private.h>

int __argc;
char **__argv;

//#define DEBUG_PRINT

#ifdef DEBUG_PRINT
#define	dprintf(...) printf(__VA_ARGS__)
#define	eprintf(...) printf(__VA_ARGS__)
#else
#define dprintf(...) do { if (0) printf(__VA_ARGS__); } while (0)
#define	eprintf(...) printf(__VA_ARGS__)
#endif

#define IHKCONFIG_CHKANDJUMP(cond, func, err)							\
	do {																\
		if(cond) {														\
			eprintf("%s,"func" failed\n", __FUNCTION__);				\
			ret = err;													\
			goto fn_fail;												\
		}																\
	} while(0)

static int usage(char **arg)
{
	char	*cmd;

	cmd = strrchr(arg[0], '/');
	if(cmd)
		cmd++;
	else
		cmd = arg[0];
	fprintf(stderr, "Usage: %s (dev #) (action)\n", cmd);
	fprintf(stderr, "action:\n");
	fprintf(stderr, "    create\n");
	fprintf(stderr, "    destroy\n");
	fprintf(stderr, "    scratch\n");
	fprintf(stderr, "    sbox\n");
	fprintf(stderr, "    read\n");
	fprintf(stderr, "    mmap\n");
	fprintf(stderr, "    ioctl\n");
	fprintf(stderr, "    clear_kmsg\n");
	fprintf(stderr, "    clear_kmsg_write\n");
	fprintf(stderr, "    reserve cpu|mem [resources]\n");
	fprintf(stderr, "    release cpu|mem [resources]\n");
	fprintf(stderr, "    query cpu|mem\n");
	fprintf(stderr, "    get os_instances\n");
	fprintf(stderr, "    get buildid\n");
	return 0;
}

static int do_destroy(int fd)
{
	int os;
	int r;

	if (__argc < 4) {
		printf("Usage: %s (dev #) destroy (os #)\n", __argv[0]);
		return 1;
	}

	os = atoi(__argv[3]);
	r = ioctl(fd, IHK_DEVICE_DESTROY_OS, os);
	if (r != 0) {
		fprintf(stderr, "error: destroying OS instance %d\n", os);
	}
	dprintf("ret = %d\n", r);
	return r;
}

static int do_read(int fd)
{
	unsigned long adr;
	unsigned char buf[16];
	int i;

	if (__argc > 3) {
		adr = strtol(__argv[3], NULL, 16);
	} else {
		fprintf(stderr, "Address is not specified!\n");
		return 1;
	}
	
	i = pread(fd, buf, sizeof(buf), adr);
	if (i < 0) {
		perror("pread");
		return 1;
	}

	for (i = 0; i < sizeof(buf); i++) {
		printf("%02x ", buf[i]);
	}
	printf("\n");
	return 0;
}

static int do_mmap(int fd)
{
	unsigned long adr;
	unsigned char *p;
	int i;

	if (__argc > 3) {
		adr = strtol(__argv[3], NULL, 16);
	} else {
		fprintf(stderr, "Address is not specified!\n");
		return 1;
	}

	p = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, adr);
	if (p == MAP_FAILED) {
		perror("mmap");
		return 1;
	}

	for (i = 0; i < 16; i++) {
		printf("%02x ", p[i]);
	}
	printf("\n");

	munmap(p, 4096);
	return 0;
}

static int do_clear_kmsg(int fd)
{
	unsigned long adr;
	unsigned char *p;

	if (__argc > 3) {
		adr = strtol(__argv[3], NULL, 16);
	} else {
		fprintf(stderr, "Address is not specified!\n");
		return 1;
	}

	p = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, adr);
	if (p == MAP_FAILED) {
		perror("mmap");
		return 1;
	}

	printf("Before write : %d\n", *(unsigned int *)p);
	*(unsigned int *)p = 0;
	munmap(p, 4096);
	return 0;
}

static int do_clear_kmsg_write(int fd)
{
	unsigned long adr;
	unsigned int l;
	int i;

	if (__argc > 3) {
		adr = strtol(__argv[3], NULL, 16);
	} else {
		fprintf(stderr, "Address is not specified!\n");
		return 1;
	}
	
	l = 0;
	i = pwrite(fd, &l, sizeof(l), adr);
	if (i < 0) {
		perror("pwrite");
		return 1;
	}

	return 0;
}

static int do_create(int fd)
{
	int r = ioctl(fd, IHK_DEVICE_CREATE_OS, 0);
	if (r != 0) {
		fprintf(stderr, "error: creating OS instance\n");
	}
	dprintf("ret = %d\n", r);
	return r;
}
static int do_scratch(int fd)
{
	int i;
	long r;

	for (i = 0; i < 16; i++) {
		r = ioctl(fd, IHK_DEVICE_DEBUG_START + 0, i);
		printf("Scratch %2d = %08lx\n", i, r);
	}
	return r;
}
static int do_sbox(int fd)
{
	int idx;
	long r;

	if (__argc > 3) {
		idx = strtol(__argv[3], NULL, 16);
	} else {
		idx = 0x1030;
	}

	r = ioctl(fd, IHK_DEVICE_DEBUG_START + 1, idx);
	printf("SBOX %04x = %08lx\n", idx, r);
	return r;
}

static int do_reserve(int fd)
{
	int ret, cnt;
	struct ihk_cpu_req req_cpu = { 0 };
	struct ihk_mem_req req_mem = { 0 };

	if (__argc < 5) {
		usage(__argv);
		return -1;
	}

	if (!strcmp(__argv[3], "cpu")) {
		/* Parse CPU list */
		cnt = cpu_str2count(__argv[4]);
		IHKCONFIG_CHKANDJUMP(cnt <= 0,
				"get num of requested cpus", -1);

		req_cpu.cpus = calloc(sizeof(int), cnt);
		IHKCONFIG_CHKANDJUMP(!req_cpu.cpus,
				"allocate request space", -1);

		ret = cpu_str2req(__argv[4], cnt, &req_cpu);
		IHKCONFIG_CHKANDJUMP(ret < 0,
				"parse provided cpulist string", -1);

		ret = ioctl(fd, IHK_DEVICE_RESERVE_CPU, &req_cpu);
		if (ret != 0) {
			fprintf(stderr, "error: reserving CPUs: %s\n", __argv[4]);
		}
	}
	else if (!strcmp(__argv[3], "mem")) {
		/* Parse memory list */
		cnt = mem_str2count(__argv[4]);
		IHKCONFIG_CHKANDJUMP(cnt <= 0,
				"get num of requested mems", -1);

		req_mem.sizes = calloc(sizeof(ssize_t), cnt);
		IHKCONFIG_CHKANDJUMP(!req_mem.sizes,
				"allocate request space", -1);

		req_mem.numa_ids = calloc(sizeof(int), cnt);
		IHKCONFIG_CHKANDJUMP(!req_mem.numa_ids,
				"allocate request space", -1);

		ret = mem_str2req(__argv[4], cnt, &req_mem);
		IHKCONFIG_CHKANDJUMP(ret < 0,
				"parse provided memlist string", -1);

		req_mem.min_chunk_size = reserve_mem_conf.min_chunk_size;
		req_mem.max_size_ratio_all =
			reserve_mem_conf.max_size_ratio_all;
		req_mem.timeout = reserve_mem_conf.timeout;

		ret = ioctl(fd, IHK_DEVICE_RESERVE_MEM, &req_mem);
		if (ret != 0) {
			fprintf(stderr, "error: reserving memory: %s\n", __argv[4]);
		}
	}
	else {
		usage(__argv);
		ret = -EINVAL;
	}

 fn_exit:
	free(req_cpu.cpus);
	free(req_mem.sizes);
	free(req_mem.numa_ids);
	dprintf("ret = %d\n", ret);
	return ret;
 fn_fail:
	goto fn_exit;
}

static int do_release(int fd)
{
	int ret, cnt;
	struct ihk_cpu_req req_cpu = { 0 };
	struct ihk_mem_req req_mem = { 0 };

	if (__argc < 5) {
		usage(__argv);
		return -1;
	}

	if (!strcmp(__argv[3], "cpu")) {
		/* Parse CPU list */
		cnt = cpu_str2count(__argv[4]);
		IHKCONFIG_CHKANDJUMP(cnt <= 0,
				"get num of requested cpus", -1);

		req_cpu.cpus = calloc(sizeof(int), cnt);
		IHKCONFIG_CHKANDJUMP(!req_cpu.cpus,
				"allocate request space", -1);

		ret = cpu_str2req(__argv[4], cnt, &req_cpu);
		IHKCONFIG_CHKANDJUMP(ret < 0,
				"parse provided cpulist string", -1);

		ret = ioctl(fd, IHK_DEVICE_RELEASE_CPU, &req_cpu);
		if (ret != 0) {
			fprintf(stderr, "error: releasing CPUs: %s\n", __argv[4]);
		}
	}
	else if (!strcmp(__argv[3], "mem")) {
		if (!strcmp(__argv[4], "all")) {
			/* Special case for releasing all memory */

			/* to get num of mem_chunks */
			ret = ioctl(fd, IHK_DEVICE_QUERY_MEM, &req_mem);
			if (ret != 0) {
				fprintf(stderr, "error: get mem chunks\n");
			}
			cnt = req_mem.num_chunks;

			req_mem.sizes = calloc(sizeof(ssize_t), cnt);
			IHKCONFIG_CHKANDJUMP(!req_mem.sizes,
					"allocate request space", -1);

			req_mem.numa_ids = calloc(sizeof(int), cnt);
			IHKCONFIG_CHKANDJUMP(!req_mem.numa_ids,
					"allocate request space", -1);

			ret = ioctl(fd, IHK_DEVICE_QUERY_MEM, &req_mem);
			if (ret != 0) {
				fprintf(stderr, "error: querying memory\n");
			}
		}
		else {
			/* Parse memory list */
			cnt = mem_str2count(__argv[4]);
			IHKCONFIG_CHKANDJUMP(cnt <= 0,
					"get num of requested mems", -1);

			req_mem.sizes = calloc(sizeof(ssize_t), cnt);
			IHKCONFIG_CHKANDJUMP(!req_mem.sizes,
					"allocate request space", -1);

			req_mem.numa_ids = calloc(sizeof(int), cnt);
			IHKCONFIG_CHKANDJUMP(!req_mem.numa_ids,
					"allocate request space", -1);

			ret = mem_str2req(__argv[4], cnt, &req_mem);
			IHKCONFIG_CHKANDJUMP(ret < 0,
					"parse provided memlist string", -1);
		}

		ret = ioctl(fd, IHK_DEVICE_RELEASE_MEM, &req_mem);
		if (ret != 0) {
			fprintf(stderr, "error: releasing memory: %s\n", __argv[4]);
		}
	}
	else {
		usage(__argv);
		ret = -EINVAL;
	}

 fn_exit:
	free(req_cpu.cpus);
	free(req_mem.sizes);
	free(req_mem.numa_ids);
	dprintf("ret = %d\n", ret);
	return ret;
 fn_fail:
	goto fn_exit;
}

static int do_query(int fd)
{
	int cnt, ret;
	struct ihk_cpu_req req_cpu = { 0 };
	struct ihk_mem_req req_mem = { 0 };
	char *query_result = NULL;

	if (__argc < 4) {
		usage(__argv);
		return -1;
	}

	if (!strcmp(__argv[3], "cpu")) {
		req_cpu.num_cpus = ioctl(fd, IHK_DEVICE_GET_NUM_CPUS);
		if (req_cpu.num_cpus < 0) {
			fprintf(stderr, "error: querying num CPUs\n");
		}

		req_cpu.cpus = calloc(sizeof(int), req_cpu.num_cpus);
		IHKCONFIG_CHKANDJUMP(!req_cpu.cpus,
				"allocate request space", -1);

		ret = ioctl(fd, IHK_DEVICE_QUERY_CPU, &req_cpu);

		if (ret != 0) {
			fprintf(stderr, "error: querying CPUs\n");
		}

		query_result = cpu_req2str(&req_cpu);
		IHKCONFIG_CHKANDJUMP(!query_result,
				"build result string", -1);
	}
	else if (!strcmp(__argv[3], "mem")) {
		/* to get num of mem chunks */
		req_mem.num_chunks = 0;
		ret = ioctl(fd, IHK_DEVICE_QUERY_MEM, &req_mem);
		if (ret != 0) {
			fprintf(stderr, "error: get mem chunks\n");
		}
		cnt = req_mem.num_chunks;

		req_mem.sizes = calloc(sizeof(ssize_t), cnt);
		IHKCONFIG_CHKANDJUMP(!req_mem.sizes,
				"allocate request space", -1);

		req_mem.numa_ids = calloc(sizeof(int), cnt);
		IHKCONFIG_CHKANDJUMP(!req_mem.numa_ids,
				"allocate request space", -1);

		ret = ioctl(fd, IHK_DEVICE_QUERY_MEM, &req_mem);

		if (ret != 0) {
			fprintf(stderr, "error: querying memory\n");
		}

		query_result = mem_req2str(&req_mem);
		IHKCONFIG_CHKANDJUMP(!query_result,
				"build result string", -1);
	}
	else {
		usage(__argv);
		ret = -EINVAL;
	}

	if (ret == 0) {
		printf("%s\n", query_result);
	}

 fn_exit:
	free(req_cpu.cpus);
	free(req_mem.sizes);
	free(req_mem.numa_ids);
	free(query_result);
	dprintf("ret = %d\n", ret);
	return ret;
 fn_fail:
	goto fn_exit;
}

static int do_ioctl(int fd)
{
	unsigned int req;
	unsigned long arg;
	long r;

	if (__argc <= 4) {
		fprintf(stderr, "No req or arg is specified.\n");
		return 1;
	}
	req = strtol(__argv[3], NULL, 16);
	arg = strtoll(__argv[4], NULL, 16);

	r = ioctl(fd, req, arg);
	if (r != 0) {
		fprintf(stderr, "error: ioctl()\n");
	}
	dprintf("ret = %lx (%ld)\n", r, r);
	return r;
}

static int do_get_os_instances(int index)
{
	int *indices = NULL;
	int num_os_instances = 0;
	int ret = 0, ret_ihklib;
	int i;

	num_os_instances = ihk_get_num_os_instances(index);
	IHKCONFIG_CHKANDJUMP(num_os_instances < 0, "ihk_get_num_os_instances", -1);

	indices = (int *)calloc(num_os_instances, sizeof(int));
	IHKCONFIG_CHKANDJUMP(indices == NULL, "calloc", -1);

	ret_ihklib = ihk_get_os_instances(index, indices, num_os_instances);
	IHKCONFIG_CHKANDJUMP(ret_ihklib < 0, "ihk_get_os_instances", -1);
	
	for (i = 0; i < num_os_instances; i++) {
		if(i != 0) {
			printf (",");
		}	
		printf ("%d", indices[i]);
	}		
	printf ("\n");
fn_exit:
	if(indices) {
		free(indices);
	}
	return ret;
 fn_fail:
	goto fn_exit;
}

static int do_get_buildid(int index)
{
	int ret = 0;
	int fd = -1;
	char fn[128];
	char query_result[sizeof(BUILDID)];

	sprintf(fn, "/dev/mcd%d", index);

	fd = open(fn, O_RDONLY);
	IHKCONFIG_CHKANDJUMP(fd < 0, "open", -1);

	ret = ioctl(fd, IHK_DEVICE_GET_BUILDID, query_result);
	IHKCONFIG_CHKANDJUMP(ret != 0, "IHK_DEVICE_GET_BUILDID", -1);

	printf("%s\n", query_result);

 fn_exit:
	if (fd != -1) {
		close(fd);
	}
	return ret;
 fn_fail:
	goto fn_exit;
}

static int do_get(int index)
{
	if (__argc < 4) {
		usage(__argv);
		return -1;
	}

	if (!strcmp(__argv[3], "os_instances")) {
		return do_get_os_instances(index);
	} else if (!strcmp(__argv[3], "buildid")) {
		return do_get_buildid(index);
	} else {
		fprintf(stderr, "Unknown target : %s\n", __argv[3]);
		usage(__argv);
		return -1;
	}
}


#define HANDLER_WITH_INDEX(name) if (!strcmp(argv[2], #name)) { int r = do_##name(atoi(argv[1])); return r; }
#define HANDLER(name) if (!strcmp(argv[2], #name)) { int r = do_##name(fd); close(fd); return r; }
int main(int argc, char **argv)
{
	int fd;
	char fn[128];

	__argc = argc;
	__argv = argv;

	if (argc < 3) {
		usage(argv);
		return 1;
	}

	HANDLER_WITH_INDEX(get)

	sprintf(fn, "/dev/mcd%d", atoi(argv[1]));

	fd = open(fn, O_RDWR);
	if (fd < 0) {
		perror("open");
		return 1;
	}

	HANDLER(create) 
	else HANDLER(destroy)
    else HANDLER(scratch)
	else HANDLER(sbox)
	else HANDLER(read)
	else HANDLER(mmap)
	else HANDLER(ioctl)
	else HANDLER(clear_kmsg)
	else HANDLER(clear_kmsg_write)
	else HANDLER(reserve)
	else HANDLER(release)
	else HANDLER(query)
	else {
		fprintf(stderr, "Unknown action : %s\n", argv[2]);
		usage(argv);
	}
	
	close(fd);
	return 0;
}
