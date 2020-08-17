/* ihkosctl.c COPYRIGHT FUJITSU LIMITED 2015-2016 */
/**
 * \file ihkosctl.c
 *  License details are found in the file LICENSE.
 * \brief
 *  configures the OSs on coprocessors
 * \author Taku Shimosawa  <shimosawa@is.s.u-tokyo.ac.jp> \par
 * \author Balazs Gerofi  <bgerofi@riken.jp> \par
 * Copyright (C) 2011-2017 RIKEN AICS>
 */
#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include "ihk/ihk_host_user.h"
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <linux/limits.h>
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

#define IHKOSCTL_CHKANDJUMP(cond, func, err)							\
	do {																\
		if(cond) {														\
			eprintf("%s,"func" failed\n", __FUNCTION__);				\
			ret = err;													\
			goto fn_fail;												\
		}																\
	} while(0)


#define PHYSMEM_NAME_SIZE 32

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
	fprintf(stderr, "    load (kernel.img)\n");
	fprintf(stderr, "    boot\n");
	fprintf(stderr, "    shutdown\n");
	fprintf(stderr, "    assign cpu|mem \n");
	fprintf(stderr, "           cpu (cpu_list) \n");
	fprintf(stderr, "           mem (size@NUMA) \n");
	fprintf(stderr, "    release cpu|mem \n");
	fprintf(stderr, "            cpu (cpu_list) \n");
	fprintf(stderr, "            mem (size@NUMA) \n");
	fprintf(stderr, "    set ikc_map (cpu_list:cpu+cpu_list:cpu+..) \n");
	fprintf(stderr, "    get ikc_map\n");
	fprintf(stderr, "    query [cpu|mem]\n");
	fprintf(stderr, "    query_free_mem\n");
	fprintf(stderr, "    kargs (kernel arg)\n");
	fprintf(stderr, "    get status\n");
	fprintf(stderr, "    kmsg\n");
	fprintf(stderr, "    clear_kmsg\n");
	fprintf(stderr, "    intr cpu irq_vector\n");
	fprintf(stderr, "    ioctl (req) (arg)\n");
#ifdef ENABLE_MEMDUMP
	fprintf(stderr, "    dump [-d level] [file]\n");
#endif /* ENABLE_MEMDUMP */

	return 0;
}

static int do_boot(int fd)
{
	int r = ioctl(fd, IHK_OS_BOOT, 0);
	if (r != 0) {
		fprintf(stderr, "error: booting\n");
	}
	dprintf("ret = %d\n", r);
	return r;
}

static int do_get_status(int index)
{
	int ret = 0, ret_ihklib;
	
	ret_ihklib = ihk_os_get_status(index);
	IHKOSCTL_CHKANDJUMP(ret_ihklib < 0, "error: ihk_os_get_status", -1);

	switch (ret_ihklib) {
		case IHK_STATUS_INACTIVE:
			printf("INACTIVE\n");
			break;
		case IHK_STATUS_BOOTING:
			printf("BOOTING\n");
			break;
		case IHK_STATUS_RUNNING:
			printf("RUNNING\n");
			break;
		case IHK_STATUS_SHUTDOWN:
			printf("SHUTDOWN\n");
			break;
		case IHK_STATUS_PANIC:
			printf("PANIC\n");
			break;
		case IHK_STATUS_HUNGUP:
			printf("HUNGUP\n");
			break;
		case IHK_STATUS_FREEZING:
			printf("FREEZING\n");
			break;
		case IHK_STATUS_FROZEN:
			printf("FROZEN\n");
			break;
	}	

 fn_exit:
	return ret;
 fn_fail:
	goto fn_exit;
}

static int do_get_ikc_map(int index)
{
	int ret = 0, ret_ioctl;
	char *query_result = NULL;
	int fd = -1;
    char fn[128];
	struct ihk_ikc_req req_ikc;

	sprintf(fn, "/dev/mcos%d", atoi(__argv[1]));

	fd = open(fn, O_RDONLY);
	if (fd < 0) {
		perror("open");
		ret = -1;
		goto fn_fail;
	}

	req_ikc.src_cpus = calloc(sizeof(int), IHK_MAX_NUM_CPUS);
	IHKOSCTL_CHKANDJUMP(!req_ikc.src_cpus, "allocate request space", -1);

	req_ikc.dst_cpus = calloc(sizeof(int), IHK_MAX_NUM_CPUS);
	IHKOSCTL_CHKANDJUMP(!req_ikc.dst_cpus, "allocate request space", -1);

	req_ikc.num_cpus = IHK_MAX_NUM_CPUS;

	ret_ioctl = ioctl(fd, IHK_OS_GET_IKC_MAP, &req_ikc);
    IHKOSCTL_CHKANDJUMP(ret_ioctl != 0, "ioctl", -1);

	query_result = ikc_req2str(&req_ikc);
	IHKOSCTL_CHKANDJUMP(!query_result, "build result string", -1);

	printf("%s\n", query_result);

 fn_exit:
	free(req_ikc.src_cpus);
	free(req_ikc.dst_cpus);
	free(query_result);
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

	sprintf(fn, "/dev/mcos%d", index);

	fd = open(fn, O_RDONLY);
	IHKOSCTL_CHKANDJUMP(fd < 0, "open", -1);

	ret = ioctl(fd, IHK_OS_GET_BUILDID, query_result);
    IHKOSCTL_CHKANDJUMP(ret != 0, "IHK_OS_GET_BUILDID", -1);

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

	if (!strcmp(__argv[3], "status")) {
		return do_get_status(index);
	} else if (!strcmp(__argv[3], "ikc_map")) {
		return do_get_ikc_map(index);
	} else if (!strcmp(__argv[3], "buildid")) {
		return do_get_buildid(index);
	} else {
        fprintf(stderr, "Unknown target : %s\n", __argv[3]);
		usage(__argv);
		return -1;
    }
}

static int do_load(int fd)
{
	char *fn;
	if (__argc > 3) {
		fn = __argv[3];
	} else {
		fn = "/home/shimosawa/mcos/mcos.image";
	}
	int r = ioctl(fd, IHK_OS_LOAD, (unsigned long)fn);

	if (r != 0) {
		fprintf(stderr, "error: loading %s\n", fn);
	}
	dprintf("ret = %d\n", r);
	return r;
}

static int do_shutdown(int fd)
{
	int r = ioctl(fd, IHK_OS_SHUTDOWN, 0);
	dprintf("ret = %d\n", r);
	if (r != 0) {
		fprintf(stderr, "error: shutting down\n");
	}
	return r;
}

static int do_alloc(int fd)
{
	int r;
	int n = 3;
	unsigned long size = 0x10000000;

	if (__argc > 3)
		n = atoi(__argv[3]);

	if (__argc > 4){
		char	*t;
		size = strtoul(__argv[4], &t, 0);
		switch(tolower(*t)){
		    case 'g':
			size *= 1024;
		    case 'm':
			size *= 1024;
		    case 'k':
			size *= 1024;
		}
	}

	r = ioctl(fd, IHK_OS_ALLOC_CPU, n);
	if (r != 0) {
		fprintf(stderr, "error: allocating CPUs\n");
		return r;
	}
	dprintf("ret[cpu] = %d\n", r);

	r = ioctl(fd, IHK_OS_ALLOC_MEM, size);
	if (r != 0) {
		fprintf(stderr, "error: allocating memory\n");
	}
	dprintf("ret[mem] = %d\n", r);
	return r;
}

static int do_reserve_cpu(int fd)
{
	int i, n = __argc - 3, r;
	int *param;

	if (n <= 0) {
		printf("No CPU is specified.\n");
		return 1;
	}

	param = malloc(sizeof(int) * (n + 1));
	param[0] = n;
	for (i = 0; i < n; i++) {
		param[i + 1] = atoi(__argv[i + 3]);
	}

	r = ioctl(fd, IHK_OS_RESERVE_CPU, (unsigned long)param);
	if (r != 0) {
		fprintf(stderr, "error: reserving CPUs\n");
	}
	dprintf("ret[cpu] = %d\n", r);
	return r;
}

static int do_reserve_mem(int fd)
{
	int r;
	unsigned long arg[2];

	if (__argc <= 4) {
		printf("Start or size is not specified.\n");
		return 1;
	}
	arg[0] = strtol(__argv[3], NULL, 16);
	arg[1] = strtoll(__argv[4], NULL, 16);

	r = ioctl(fd, IHK_OS_RESERVE_MEM, (unsigned long)arg);
	if (r != 0) {
		fprintf(stderr, "error: reserving memory\n");
	}
	dprintf("ret[mem] = %d\n", r);
	return r;
}

static int do_set_ikc_map(int fd)
{
	int ret, cnt;
	struct ihk_ikc_req req_ikc = { 0 };

	if (__argc < 5) {
		usage(__argv);
		return -1;
	}

	/* Parse ikc_map list */
	cnt = ikc_str2count(__argv[4]);
	IHKOSCTL_CHKANDJUMP(cnt <= 0,
			"get num of requested maps", -1);

	req_ikc.src_cpus = calloc(sizeof(int), cnt);
	IHKOSCTL_CHKANDJUMP(!req_ikc.src_cpus,
			"allocate request space", -1);

	req_ikc.dst_cpus = calloc(sizeof(int), cnt);
	IHKOSCTL_CHKANDJUMP(!req_ikc.dst_cpus,
			"allocate request space", -1);

	ret = ikc_str2req(__argv[4], cnt, &req_ikc);
	IHKOSCTL_CHKANDJUMP(ret < 0,
			"parse provided memlist string", -1);

	ret = ioctl(fd, IHK_OS_SET_IKC_MAP, &req_ikc);
	if (ret != 0) {
		fprintf(stderr, "error: setting up IKC map: %s\n", __argv[4]);
	}

 fn_exit:
	free(req_ikc.src_cpus);
	free(req_ikc.dst_cpus);
	dprintf("ret = %d\n", ret);
	return ret;
 fn_fail:
	goto fn_exit;
}

static int do_set(int fd)
{
	if (__argc < 4) {
		usage(__argv);
		return -1;
	}

	if (!strcmp(__argv[3], "ikc_map")) {
		return do_set_ikc_map(fd);
	} else {
        fprintf(stderr, "Unknown target : %s\n", __argv[3]);
		usage(__argv);
		return -1;
    }
}


static int do_assign(int fd)
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
		IHKOSCTL_CHKANDJUMP(cnt <= 0,
				"get num of requested cpus", -1);

		req_cpu.cpus = calloc(sizeof(int), cnt);
		IHKOSCTL_CHKANDJUMP(!req_cpu.cpus,
				"allocate request space", -1);

		ret = cpu_str2req(__argv[4], cnt, &req_cpu);
		IHKOSCTL_CHKANDJUMP(ret < 0,
				"parse provided cpulist string", -1);

		ret = ioctl(fd, IHK_OS_ASSIGN_CPU, &req_cpu);
		if (ret != 0) {
			fprintf(stderr, "error: assigning CPUs: %s\n", __argv[4]);
		}
	}
	else if (!strcmp(__argv[3], "mem")) {
		/* Parse memory list */
		cnt = mem_str2count(__argv[4]);
		IHKOSCTL_CHKANDJUMP(cnt <= 0,
				"get num of requested mems", -1);

		req_mem.sizes = calloc(sizeof(ssize_t), cnt);
		IHKOSCTL_CHKANDJUMP(!req_mem.sizes,
				"allocate request space", -1);

		req_mem.numa_ids = calloc(sizeof(int), cnt);
		IHKOSCTL_CHKANDJUMP(!req_mem.numa_ids,
				"allocate request space", -1);

		ret = mem_str2req(__argv[4], &req_mem);
		IHKOSCTL_CHKANDJUMP(ret < 0,
				"parse provided memlist string", -1);

		ret = ioctl(fd, IHK_OS_ASSIGN_MEM, &req_mem);
		if (ret != 0) {
			fprintf(stderr, "error: assiging memory: %s\n",
				__argv[4]);
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
		IHKOSCTL_CHKANDJUMP(cnt <= 0,
				"get num of requested cpus", -1);

		req_cpu.cpus = calloc(sizeof(int), cnt);
		IHKOSCTL_CHKANDJUMP(!req_cpu.cpus,
				"allocate request space", -1);

		ret = cpu_str2req(__argv[4], cnt, &req_cpu);
		IHKOSCTL_CHKANDJUMP(ret < 0,
				"parse provided cpulist string", -1);

		ret = ioctl(fd, IHK_OS_RELEASE_CPU, &req_cpu);
		if (ret != 0) {
			fprintf(stderr, "error: releasing CPUs: %s\n", __argv[4]);
		}
	}
	else if (!strcmp(__argv[3], "mem")) {
		if (!strcmp(__argv[4], "all")) {
			/* Special case for releasing all memory */

			/* to get num of mem_chunks */
			ret = ioctl(fd, IHK_OS_QUERY_MEM, &req_mem);
			if (ret != 0) {
				fprintf(stderr, "error: get mem chunks\n");
			}
			cnt = req_mem.num_chunks;

			req_mem.sizes = calloc(sizeof(ssize_t), cnt);
			IHKOSCTL_CHKANDJUMP(!req_mem.sizes,
					"allocate request space", -1);

			req_mem.numa_ids = calloc(sizeof(int), cnt);
			IHKOSCTL_CHKANDJUMP(!req_mem.numa_ids,
					"allocate request space", -1);

			ret = ioctl(fd, IHK_OS_QUERY_MEM, &req_mem);
			if (ret != 0) {
				fprintf(stderr, "error: querying memory\n");
			}
		}
		else {
			/* Parse memory list */
			cnt = mem_str2count(__argv[4]);
			IHKOSCTL_CHKANDJUMP(cnt <= 0,
					"get num of requested mems", -1);

			req_mem.sizes = calloc(sizeof(ssize_t), cnt);
			IHKOSCTL_CHKANDJUMP(!req_mem.sizes,
					"allocate request space", -1);

			req_mem.numa_ids = calloc(sizeof(int), cnt);
			IHKOSCTL_CHKANDJUMP(!req_mem.numa_ids,
					"allocate request space", -1);

			ret = mem_str2req(__argv[4], &req_mem);
			IHKOSCTL_CHKANDJUMP(ret < 0,
					"parse provided memlist string", -1);
		}

		ret = ioctl(fd, IHK_OS_RELEASE_MEM, &req_mem);
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
		cnt = ioctl(fd, IHK_OS_GET_NUM_CPUS);
		if (cnt < 0) {
			fprintf(stderr, "error: querying num CPUs\n");
		}

		req_cpu.num_cpus = cnt;
		req_cpu.cpus = calloc(sizeof(int), cnt);
		IHKOSCTL_CHKANDJUMP(!req_cpu.cpus,
				"allocate request space", -1);

		ret = ioctl(fd, IHK_OS_QUERY_CPU, &req_cpu);

		if (ret != 0) {
			fprintf(stderr, "error: querying CPUs\n");
		}

		query_result = cpu_req2str(&req_cpu);
		IHKOSCTL_CHKANDJUMP(!query_result,
				"build result string", -1);
	}
	else if (!strcmp(__argv[3], "mem")) {
		/* to get num of mem chunks */
		req_mem.num_chunks = 0;
		ret = ioctl(fd, IHK_OS_QUERY_MEM, &req_mem);
		if (ret != 0) {
			fprintf(stderr, "error: get mem chunks\n");
		}
		cnt = req_mem.num_chunks;

		req_mem.sizes = calloc(sizeof(ssize_t), cnt);
		IHKOSCTL_CHKANDJUMP(!req_mem.sizes,
				"allocate request space", -1);

		req_mem.numa_ids = calloc(sizeof(int), cnt);
		IHKOSCTL_CHKANDJUMP(!req_mem.numa_ids,
				"allocate request space", -1);

		ret = ioctl(fd, IHK_OS_QUERY_MEM, &req_mem);

		if (ret != 0) {
			fprintf(stderr, "error: querying memory\n");
		}

		query_result = mem_req2str(&req_mem);
		IHKOSCTL_CHKANDJUMP(!query_result,
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

#define RESULT_LEN	16384
static int do_query_free_mem(int fd)
{
	int ret;
	char result[RESULT_LEN];

	ret = ihklib_os_query_mem_sysfs(atol(__argv[1]), result,
					sizeof(result),
					ihklib_os_query_mem_type_str[IHKLIB_OS_QUERY_MEM_FREE]);
	IHKOSCTL_CHKANDJUMP(ret != 0,
			    "ihklib_os_query_mem_sysfs", -1);
	printf("%s\n", result);

	ret = 0;
 fn_exit:
	return ret;
 fn_fail:
	goto fn_exit;
}


static int do_intr(int fd)
{
	int r;
	int v = 0xf1;
	int c = 0;
	if (__argc > 3) {
		v = atoi(__argv[3]);
	}
	if (__argc > 4) {
		c = atoi(__argv[3]);
		v = atoi(__argv[4]);
	}
	dprintf("sending IRQ %d to core %d\n", v, c);
	r = ioctl(fd, IHK_OS_DEBUG_START, ((c << 8) | v));
	if (r != 0) {
		fprintf(stderr, "error: sending IRQ\n");
	}
	dprintf("ret = %d\n", r);
	return 0;
}

static int do_kargs(int fd)
{
	int r;

	if (__argc <= 3) {
		fprintf(stderr, "error: no arg specified.\n");
		return 1;
	} 

	r = ioctl(fd, IHK_OS_SET_KARGS, (char *)__argv[3]);
	if (r != 0) {
		fprintf(stderr, "error: sending IRQ\n");
	}
	dprintf("ret = %d\n", r);
	return r;
}

static int do_kmsg(int fd)
{
	char buf[IHK_KMSG_SIZE];
	int r = ioctl(fd, IHK_OS_READ_KMSG, (unsigned long)buf);
	if (r >= 0) {
		buf[r] = 0;
		printf("%s\n", buf);
		return 0;
	}
	else {
		fprintf(stderr, "error querying kmsg\n");
		return 1;
	}
}

static int do_clear_kmsg(int fd)
{
	int r = ioctl(fd, IHK_OS_CLEAR_KMSG, 0);

	dprintf("ret = %d\n", r >= 0 ? r : -errno);
	return r >= 0 ? r : -errno;
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
		fprintf(stderr, "error: ioctl\n");
	}
	dprintf("ret = %lx\n", r);
	return r;
}

#ifdef ENABLE_MEMDUMP
#include <inttypes.h>
#include <time.h>
#include <limits.h>
#include <pwd.h>
#include <getopt.h>

static struct option do_dump_options[] = {
	{
		.name =		"interactive",
		.has_arg =	no_argument,
		.flag =		0,
		.val =		1
	},
	/* end */
	{ NULL, 0, NULL, 0}
};

static int do_dump(int os_index) {
	char path[PATH_MAX];
	char *dump_file;
	int dump_level = DUMP_LEVEL_ALL;
	int opt, interactive = 0;

	while ((opt = getopt_long(__argc, __argv, "id:", do_dump_options, NULL)) != -1) {
		switch (opt) {
			case 1:   /* '--interactive' */
			case 'i': /* '-i' */
				interactive = 1;
				break;
			case 'd': /* '-d' */
				dump_level = atoi(optarg);
				break;
			default: /* '?' */
				fprintf(stderr, "dump [-d level] [-i|--interactive] [file]\n");
				return 1;
		}
	}

	dprintf("%s: __argc=%d,optind=%d\n", __FUNCTION__, __argc, optind);
	if (__argc > (optind + 2)) {
		dump_file = __argv[optind + 2];
	} else {
		time_t t;
		struct tm *tm;
		size_t n;

		t = time(NULL);
		if (t == (time_t)-1) {
			perror("time");
			return 1;
		}
		tm = localtime(&t);
		if (!tm) {
			perror("localtime");
			return 1;
		}
		n = strftime(path, sizeof(path), "mcdump_%Y%m%d_%H%M%S", tm);

		if (!n) {
			perror("strftime");
			return 1;
		}

		dump_file = path;
	}
	dprintf("%s: os_index=%d,dump_file=%s,dump_level=%d,interactive=%d\n", __FUNCTION__, os_index, dump_file, dump_level, interactive);
	return ihk_os_makedumpfile(os_index, dump_file, dump_level, interactive);
}
#else /* ENABLE_MEMDUMP */
static int do_dump(int osfd)
{
	fprintf(stderr, "dump is not supported.\n");
	return 1;
}
#endif /* ENABLE_MEMDUMP */

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
	else HANDLER_WITH_INDEX(dump)

	sprintf(fn, "/dev/mcos%d", atoi(argv[1]));

	fd = open(fn, O_RDONLY);
	if (fd < 0) {
		perror("error: open failed");
		return 1;
	}

	HANDLER(load) 
	else HANDLER(boot) 
	else HANDLER(shutdown) 
	else HANDLER(alloc)
	else HANDLER(reserve_cpu)
	else HANDLER(reserve_mem)
	else HANDLER(assign)
	else HANDLER(release)
	else HANDLER(set)
	else HANDLER(query)
	else HANDLER(query_free_mem)
	else HANDLER(kargs)
	else HANDLER(kmsg)
	else HANDLER(clear_kmsg)
	else HANDLER(intr)
	else HANDLER(ioctl)
	else {
		fprintf(stderr, "Unknown action : %s\n", argv[2]);
		usage(argv);
	}
	
	close(fd);
	return 0;
}
