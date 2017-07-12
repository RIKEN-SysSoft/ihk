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
#include "ihklib.h"

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
	fprintf(stderr, "    ikc_map (cpu_list:cpu+cpu_list:cpu+..) \n");
	fprintf(stderr, "    query [cpu|mem]\n");
	fprintf(stderr, "    query_free_mem\n");
	fprintf(stderr, "    kargs (kernel arg)\n");
	fprintf(stderr, "    status\n");
	fprintf(stderr, "    getosinfo\n");
	fprintf(stderr, "    kmsg\n");
	fprintf(stderr, "    clear_kmsg\n");
	fprintf(stderr, "    intr cpu irq_vector\n");
	fprintf(stderr, "    ioctl (req) (arg)\n");
#ifdef ENABLE_MEMDUMP
	fprintf(stderr, "    dump [file]\n");
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

static int do_status(int fd)
{
	int r = ioctl(fd, IHK_OS_STATUS, 0);
	printf("ret = %d\n", r);
	return r;
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

static int do_ikc_map(int fd)
{
	int ret;

	if (__argc < 4) {
		usage(__argv);
		return -1;
	}

	ret = ioctl(fd, IHK_OS_IKC_MAP, __argv[3]);

	if (ret != 0) {
		fprintf(stderr, "error: setting up IKC map: %s\n", __argv[3]);
	}

	dprintf("ret = %d\n", ret);
	return ret;
}

static int do_assign(int fd)
{
	int ret;
	ihk_resource_req_t req;

	if (__argc < 5) {
		usage(__argv);
		return -1;
	}

	req.string = __argv[4];
	req.string_len = strlen(__argv[4]);
	if (!req.string || !req.string_len) {
		usage(__argv);
		return -1;
	}

	if (!strcmp(__argv[3], "cpu")) {
		ret = ioctl(fd, IHK_OS_ASSIGN_CPU, &req);

		if (ret != 0) {
			fprintf(stderr, "error: assigning CPUs: %s\n", __argv[4]);
		}
	}
	else if (!strcmp(__argv[3], "mem")) {
		ret = ioctl(fd, IHK_OS_ASSIGN_MEM, &req);

		if (ret != 0) {
			fprintf(stderr, "error: assigning memory: %s\n", __argv[4]);
		}
	}
	else {
		usage(__argv);
		ret = -EINVAL;
	}

	dprintf("ret = %d\n", ret);
	return ret;
}

static int do_release(int fd)
{
	int ret;
	ihk_resource_req_t req;

	if (__argc < 5) {
		usage(__argv);
		return -1;
	}

	req.string = __argv[4];
	req.string_len = strlen(__argv[4]);
	if (!req.string || !req.string_len) {
		usage(__argv);
		return -1;
	}

	if (!strcmp(__argv[3], "cpu")) {
		ret = ioctl(fd, IHK_OS_RELEASE_CPU, &req);

		if (ret != 0) {
			fprintf(stderr, "error: releasing CPUs: %s\n", __argv[4]);
		}
	}
	else if (!strcmp(__argv[3], "mem")) {
		ret = ioctl(fd, IHK_OS_RELEASE_MEM, &req);

		if (ret != 0) {
			fprintf(stderr, "error: releasing memory: %s\n", __argv[4]);
		}
	}
	else {
		usage(__argv);
		ret = -EINVAL;
	}

	dprintf("ret = %d\n", ret);
	return ret;
}

static int do_query(int fd)
{
	int ret; 
	char query_result[8192];

	/* Old code.. */
	if (__argc < 3) {
		int r = ioctl(fd, IHK_OS_QUERY_STATUS);
		if (r != 0) {
			fprintf(stderr, "error: querying\n");
		}
		dprintf("status = %d\n", r);

		return r;
	}
	
	memset(query_result, 0, sizeof(query_result));

	if (!strcmp(__argv[3], "cpu")) {
		ret = ioctl(fd, IHK_OS_QUERY_CPU, query_result);

		if (ret != 0) {
			fprintf(stderr, "error: querying CPUs\n");
		}
	}
	else if (!strcmp(__argv[3], "mem")) {
		ret = ioctl(fd, IHK_OS_QUERY_MEM, query_result);

		if (ret != 0) {
			fprintf(stderr, "error: querying memory\n");
		}
	}
	else {
		usage(__argv);
		ret = -EINVAL;
	}

	if (ret == 0) {
		printf("%s\n", query_result);
	}

	dprintf("ret = %d\n", ret);
	return ret;
}

#define RESULT_LEN	16384
static int do_query_free_mem(int fd)
{
	int ret = 0, ret_internal;
	char result[RESULT_LEN];
	ret_internal = _ihklib_os_query_free_mem(atol(__argv[1]), result, sizeof(result));
	IHKOSCTL_CHKANDJUMP(ret_internal != 0, "_ihklib_os_query_free_mem", -1);
	printf("%s\n", result);
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
	char buf[16384];
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

static int do_getosinfo(int fd) {
	int ret = 0, ret_ihklib;
	int i, j;
	int index = atoi(__argv[1]);

	ihk_osinfo osinfo;
	bzero(&osinfo, sizeof(osinfo));
	ret_ihklib = ihk_getosinfo(index, &osinfo);
    IHKOSCTL_CHKANDJUMP(ret_ihklib == -1, "ihk_getosinfo", -1);

    osinfo.mem_chunks = (ihk_mem_chunk *)calloc(osinfo.num_mem_chunks, sizeof(ihk_mem_chunk));
    IHKOSCTL_CHKANDJUMP(osinfo.mem_chunks == NULL, "calloc", -1);

    osinfo.cpus = (int *)calloc(osinfo.num_cpus, sizeof(int));
    IHKOSCTL_CHKANDJUMP(osinfo.cpus == NULL, "calloc", -1);

    osinfo.ikc_sset_sizes = (int*)calloc(osinfo.num_ikc_ssets, sizeof(int));
    IHKOSCTL_CHKANDJUMP(osinfo.ikc_sset_sizes == NULL, "calloc", -1);
	
    osinfo.ikc_sset_members = (int**)calloc(osinfo.num_ikc_ssets, sizeof(int*));
    IHKOSCTL_CHKANDJUMP(osinfo.ikc_sset_members == NULL, "calloc", -1);

    osinfo.ikc_map = (int*)calloc(osinfo.num_ikc_ssets, sizeof(int));
    IHKOSCTL_CHKANDJUMP(osinfo.ikc_map == NULL, "calloc", -1);

	ret_ihklib = ihk_getosinfo(index, &osinfo);
    IHKOSCTL_CHKANDJUMP(ret_ihklib == -1, "ihk_getosinfo", -1);

    for(i = 0; i < osinfo.num_ikc_ssets; i++) {
        dprintf("%s,ikc_sset_sizes[%d]=%d\n", __FILE__, i, osinfo.ikc_sset_sizes[i]);
        osinfo.ikc_sset_members[i] = calloc(osinfo.ikc_sset_sizes[i], sizeof(int));
		IHKOSCTL_CHKANDJUMP(osinfo.ikc_sset_members[i] == NULL, "calloc", -1);
    }

	ret_ihklib = ihk_getosinfo(index, &osinfo);
    IHKOSCTL_CHKANDJUMP(ret_ihklib == -1, "ihk_getosinfo", -1);

    printf("Assigned_mem: ");
    for (i = 0; i < osinfo.num_mem_chunks; i++) {
		printf("%lu@%d",
			   osinfo.mem_chunks[i].size,
			   osinfo.mem_chunks[i].numa_node_number);
		if(i != osinfo.num_mem_chunks - 1) {
            printf(",");
        }
    }
    printf("\n");

	printf("Assigned_cpu: ");
	for (i = 0; i < osinfo.num_cpus; i++) {
		printf("%d", osinfo.cpus[i]);
		if(i != osinfo.num_cpus - 1) {
			printf(",");
		}
	}
    printf("\n");

    printf("IKC_map: ");
	for (i = 0; i < osinfo.num_ikc_ssets; i++) {
		for (j = 0; j < osinfo.ikc_sset_sizes[i]; j++) {
			printf("%d", *(osinfo.ikc_sset_members[i] + j));
			if(j != osinfo.ikc_sset_sizes[i] - 1) {
				printf(",");
			}
		}
		printf(":");
		printf("%d", osinfo.ikc_map[i]);
		if(i != osinfo.num_ikc_ssets - 1) {
			printf("+");
		}
	}
    printf("\n");

	switch (osinfo.status) {
		case IHK_STATUS_INACTIVE:
			printf("Status: INACTIVE\n");
			break;
		case IHK_STATUS_BOOTING:
			printf("Status: BOOTING\n");
			break;
		case IHK_STATUS_RUNNING:
			printf("Status: RUNNING\n");
			break;
		case IHK_STATUS_SHUTDOWN:
			printf("Status: SHUTDOWN\n");
			break;
		case IHK_STATUS_PANIC:
			printf("Status: PANIC\n");
			break;
		case IHK_STATUS_HUNGUP:
			printf("Status: HUNGUP\n");
			break;
		case IHK_STATUS_FREEZING:
			printf("Status: FREEZING\n");
			break;
		case IHK_STATUS_FROZEN:
			printf("Status: FROZEN\n");
			break;
	}	

 fn_fail:
	if(osinfo.ikc_sset_members) {
		for (i = 0; i < osinfo.num_ikc_ssets; ++i) {
			if(osinfo.ikc_sset_members[i]) {
				free(osinfo.ikc_sset_members[i]);
			}
		}
	}
	if(osinfo.ikc_map) {
		free(osinfo.ikc_map);
	}
	if(osinfo.ikc_sset_members) {
		free(osinfo.ikc_sset_members);
	}
	if(osinfo.ikc_sset_sizes) {
		free(osinfo.ikc_sset_sizes);
	}
	if(osinfo.mem_chunks) {
		free(osinfo.mem_chunks);
	}

	// fn_exit:
    return ret;
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
#include <bfd.h>
#include <inttypes.h>
#include <time.h>
#include <limits.h>
#include <pwd.h>

static int do_dump(int osfd) {
	static char path[PATH_MAX];
	static char hname[HOST_NAME_MAX+1];
	bfd *abfd = NULL;
	char *fname;
	bfd_boolean ok;
	asection *scn;
	dumpargs_t args;
	unsigned long phys_size, phys_offset;
	int error, i;
	size_t bsize;
	void *buf = NULL;
	uintptr_t addr;
	size_t cpsize;
	time_t t;
	struct tm *tm;
	size_t n;
	char *date;
	struct passwd *pw;
	dump_mem_chunks_t *mem_chunks;
	int interactive = 0;

	if (__argc > 4 &&
			(!strcmp("--interactive", __argv[4]) ||
			 !strcmp("-i", __argv[4]))) {
		interactive = 1;
	}

	mem_chunks = malloc(PHYS_CHUNKS_DESC_SIZE);
	if (!mem_chunks) {
		perror("allocating mem_chunks buffer: ");
		return 1;
	}

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
	gethostname(hname, sizeof(hname));

	pw = getpwuid(getuid());

	args.cmd = DUMP_NMI;
	error = ioctl(osfd, IHK_OS_DUMP, &args);
	if (error) {
		perror("DUMP_NMI");
		return 1;
	}

	args.cmd = DUMP_QUERY;
	args.buf = (void *)mem_chunks;
	error = ioctl(osfd, IHK_OS_DUMP, &args);
	if (error) {
		perror("DUMP_QUERY");
		return 1;
	}

	phys_size = 0;
	dprintf("%s: nr chunks: %d\n", __FUNCTION__, mem_chunks->nr_chunks);
	for (i = 0; i < mem_chunks->nr_chunks; ++i) {
		dprintf("%s: 0x%lx:%lu\n",
				__FUNCTION__,
				mem_chunks->chunks[i].addr,
				mem_chunks->chunks[i].size);
		phys_size += mem_chunks->chunks[i].size;
	}

	bsize = 0x100000;
	buf = malloc(bsize);
	if (!buf) {
		perror("malloc");
		return 1;
	}

	bfd_init();

	if (__argc >= 4) {
		fname = __argv[3];
	}
	else {
		n = strftime(path, sizeof(path), "mcdump_%Y%m%d_%H%M%S", tm);
		if (!n) {
			perror("strftime");
			return 1;
		}
		fname = path;
	}

	abfd = bfd_fopen(fname, "elf64-x86-64", "w", -1);
	if (!abfd) {
		bfd_perror("bfd_fopen");
		return 1;
	}

	ok = bfd_set_format(abfd, bfd_object);
	if (!ok) {
		bfd_perror("bfd_set_format");
		return 1;
	}

	date = asctime(tm);
	if (date) {
		cpsize = strlen(date) - 1;	/* exclude trailing '\n' */
		scn = bfd_make_section_anyway(abfd, "date");
		if (!scn) {
			bfd_perror("bfd_make_section_anyway(date)");
			return 1;
		}

		ok = bfd_set_section_size(abfd, scn, cpsize);
		if (!ok) {
			bfd_perror("bfd_set_section_size");
			return 1;
		}

		ok = bfd_set_section_flags(abfd, scn, SEC_HAS_CONTENTS);
		if (!ok) {
			bfd_perror("bfd_set_setction_flags");
			return 1;
		}
	}
	error = gethostname(hname, sizeof(hname));
	if (!error) {
		cpsize = strlen(hname);
		scn = bfd_make_section_anyway(abfd, "hostname");
		if (!scn) {
			bfd_perror("bfd_make_section_anyway(hostname)");
			return 1;
		}

		ok = bfd_set_section_size(abfd, scn, cpsize);
		if (!ok) {
			bfd_perror("bfd_set_section_size");
			return 1;
		}

		ok = bfd_set_section_flags(abfd, scn, SEC_HAS_CONTENTS);
		if (!ok) {
			bfd_perror("bfd_set_setction_flags");
			return 1;
		}
	}
	pw = getpwuid(getuid());
	if (pw) {
		cpsize = strlen(pw->pw_name);
		scn = bfd_make_section_anyway(abfd, "user");
		if (!scn) {
			bfd_perror("bfd_make_section_anyway(user)");
			return 1;
		}

		ok = bfd_set_section_size(abfd, scn, cpsize);
		if (!ok) {
			bfd_perror("bfd_set_section_size");
			return 1;
		}

		ok = bfd_set_section_flags(abfd, scn, SEC_HAS_CONTENTS);
		if (!ok) {
			bfd_perror("bfd_set_setction_flags");
			return 1;
		}
	}

	/* Add section for physical memory chunks information */
	scn = bfd_make_section_anyway(abfd, "physchunks");
	if (!scn) {
		bfd_perror("bfd_make_section_anyway(physchunks)");
		return 1;
	}

	ok = bfd_set_section_size(abfd, scn, PHYS_CHUNKS_DESC_SIZE);
	if (!ok) {
		bfd_perror("bfd_set_section_size");
		return 1;
	}

	ok = bfd_set_section_flags(abfd, scn, SEC_ALLOC|SEC_HAS_CONTENTS);
	if (!ok) {
		bfd_perror("bfd_set_setction_flags");
		return 1;
	}

	/* Physical memory contents section */
	scn = bfd_make_section_anyway(abfd, "physmem");
	if (!scn) {
		bfd_perror("bfd_make_section_anyway(physmem)");
		return 1;
	}

	if (interactive) {
		ok = bfd_set_section_size(abfd, scn, 4096);
	}
	else {
		ok = bfd_set_section_size(abfd, scn, phys_size);
	}

	if (!ok) {
		bfd_perror("bfd_set_section_size");
		return 1;
	}

	ok = bfd_set_section_flags(abfd, scn, SEC_ALLOC|SEC_HAS_CONTENTS);
	if (!ok) {
		bfd_perror("bfd_set_setction_flags");
		return 1;
	}
	scn->vma = mem_chunks->chunks[0].addr;

	scn = bfd_get_section_by_name(abfd, "date");
	if (scn) {
		ok = bfd_set_section_contents(abfd, scn, date, 0, scn->size);
		if (!ok) {
			bfd_perror("bfd_set_section_contents(date)");
			return 1;
		}
	}

	scn = bfd_get_section_by_name(abfd, "hostname");
	if (scn) {
		ok = bfd_set_section_contents(abfd, scn, hname, 0, scn->size);
		if (!ok) {
			bfd_perror("bfd_set_section_contents(hostname)");
			return 1;
		}
	}

	scn = bfd_get_section_by_name(abfd, "user");
	if (scn) {
		ok = bfd_set_section_contents(abfd, scn, pw->pw_name, 0, scn->size);
		if (!ok) {
			bfd_perror("bfd_set_section_contents(user)");
			return 1;
		}
	}

	scn = bfd_get_section_by_name(abfd, "physchunks");
	if (scn) {
		ok = bfd_set_section_contents(abfd, scn, mem_chunks, 0, PHYS_CHUNKS_DESC_SIZE);
		if (!ok) {
			bfd_perror("bfd_set_section_contents(physchunks)");
			return 1;
		}
	}

	if (interactive)
		goto out;

	scn = bfd_get_section_by_name(abfd, "physmem");
	phys_offset = 0;
	for (i = 0; i < mem_chunks->nr_chunks; ++i) {

		for (addr = mem_chunks->chunks[i].addr;
				addr < (mem_chunks->chunks[i].addr + mem_chunks->chunks[i].size);
				addr += cpsize) {

			cpsize = (mem_chunks->chunks[i].addr + mem_chunks->chunks[i].size) - addr;
			if (cpsize > bsize) {
				cpsize = bsize;
			}

			args.cmd = DUMP_READ;
			args.start = addr;
			args.size = cpsize;
			args.buf = buf;

			error = ioctl(osfd, IHK_OS_DUMP, &args);
			if (error) {
				perror("DUMP_READ");
				return 1;
			}

			ok = bfd_set_section_contents(abfd, scn, buf, phys_offset, cpsize);
			if (!ok) {
				bfd_perror("bfd_set_section_contents(physmem)");
				return 1;
			}

			phys_offset += cpsize;
		}
	}

out:
	ok = bfd_close(abfd);
	if (!ok) {
		bfd_perror("bfd_close");
		return 1;
	}
	return 0;
}
#else /* ENABLE_MEMDUMP */
static int do_dump(int osfd)
{
	fprintf(stderr, "dump is not supported.\n");
	return 1;
}
#endif /* ENABLE_MEMDUMP */

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

	sprintf(fn, "/dev/mcos%d", atoi(argv[1]));

	fd = open(fn, O_RDONLY);
	if (fd < 0) {
		perror("open");
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
	else HANDLER(ikc_map)
	else HANDLER(query)
	else HANDLER(status)
	else HANDLER(getosinfo)
	else HANDLER(query_free_mem)
	else HANDLER(kargs)
	else HANDLER(kmsg)
	else HANDLER(clear_kmsg)
	else HANDLER(intr)
	else HANDLER(ioctl)
	else HANDLER(dump)
	else {
		fprintf(stderr, "Unknown action : %s\n", argv[2]);
		usage(argv);
	}
	
	close(fd);
	return 0;
}
