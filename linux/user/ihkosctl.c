/**
 * \file ihkosctl.c
 *  License details are found in the file LICENSE.
 * \brief
 *  configures the OSs on coprocessors
 * \author Taku Shimosawa  <shimosawa@is.s.u-tokyo.ac.jp> \par
 *	Copyright (C) 2011 - 2012  Taku Shimosawa
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include "ihk/ihk_host_user.h"
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>

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
	fprintf(stderr, "    alloc [cpu [memory]] \n");
	fprintf(stderr, "    reserve_cpu [cpu_num]...\n");
	fprintf(stderr, "    reserve_mem (addr) (size)\n");
	fprintf(stderr, "    assign cpu|mem (new IHK iface)\n");
	fprintf(stderr, "           cpu (cpu_list) (new IHK iface)\n");
	fprintf(stderr, "           mem (addr) (size) (new IHK iface)\n");
	fprintf(stderr, "    release cpu|mem (new IHK iface)\n");
	fprintf(stderr, "            cpu (cpu_list) (new IHK iface)\n");
	fprintf(stderr, "            mem (addr) (size) (new IHK iface)\n");
	fprintf(stderr, "    query\n");
	fprintf(stderr, "    query_free_mem\n");
	fprintf(stderr, "    kargs (kernel arg)\n");
	fprintf(stderr, "    kmsg\n");
	fprintf(stderr, "    clear_kmsg\n");
	fprintf(stderr, "    intr [intr]\n");
	fprintf(stderr, "    ioctl (req) (arg)\n");

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

static int do_assign(int fd)
{
	int ret;

	if (__argc < 5) {
		usage(__argv);
		return -1;
	}

	if (!strcmp(__argv[3], "cpu")) {
		ret = ioctl(fd, IHK_OS_ASSIGN_CPU, __argv[4]);

		if (ret != 0) {
			fprintf(stderr, "error: assigning CPUs: %s\n", __argv[4]);
		}
	}
	else if (!strcmp(__argv[3], "mem")) {
		ret = ioctl(fd, IHK_OS_ASSIGN_MEM, __argv[4]);

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

	if (__argc < 4) {
		usage(__argv);
		return -1;
	}

	if (!strcmp(__argv[3], "cpu")) {
		ret = ioctl(fd, IHK_OS_RELEASE_CPU, __argv[4]);

		if (ret != 0) {
			fprintf(stderr, "error: releasing CPUs: %s\n", __argv[4]);
		}
	}
	else if (!strcmp(__argv[3], "mem")) {
		ret = ioctl(fd, IHK_OS_RELEASE_MEM, __argv[4]);

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
	int r = ioctl(fd, IHK_OS_QUERY_STATUS);
	if (r != 0) {
		fprintf(stderr, "error: querying\n");
	}
	dprintf("status = %d\n", r);
	return 0;
}

static int do_query_free_mem(int fd)
{
	int r = ioctl(fd, IHK_OS_QUERY_FREE_MEM);
	
	if (r != 0) {
		fprintf(stderr, "error: querying free memory\n");
	}

	printf("number of free pages (4kB): %d\n", r);
	return r;
}


static int do_intr(int fd)
{
	int r;
	int v = 0xf1;
	if (__argc > 3) {
		v = atoi(__argv[3]);
	}
	r = ioctl(fd, IHK_OS_DEBUG_START, v);
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
