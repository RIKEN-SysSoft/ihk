/**
 * \file ihkconfig.c
 *  License details are found in the file LICENSE.
 * \brief
 *  configures the IHK device
 * \author Taku Shimosawa  <shimosawa@is.s.u-tokyo.ac.jp> \par
 * \author Balazs Gerofi  <bgerofi@riken.jp> \par
 * Copyright (C) 2011-2017 RIKEN AICS>
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include "ihk/ihk_host_user.h"
#include <sys/ioctl.h>
#include <sys/mman.h>
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
	fprintf(stderr, "    release cpu [resources]\n");
	fprintf(stderr, "    release mem\n");
	fprintf(stderr, "    query cpu|mem\n");

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
		ret = ioctl(fd, IHK_DEVICE_RESERVE_CPU, &req);

		if (ret != 0) {
			fprintf(stderr, "error: reserving CPUs: %s\n", __argv[4]);
		}
	}
	else if (!strcmp(__argv[3], "mem")) {
		ret = ioctl(fd, IHK_DEVICE_RESERVE_MEM, &req);

		if (ret != 0) {
			fprintf(stderr, "error: reserving memory: %s\n", __argv[4]);
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

	if (__argc < 4) {
		usage(__argv);
		return -1;
	}

	req.string = __argv[4];
	req.string_len = __argv[4] ? strlen(__argv[4]) : 0;

	if (!strcmp(__argv[3], "cpu")) {
		ret = ioctl(fd, IHK_DEVICE_RELEASE_CPU, &req);

		if (ret != 0) {
			fprintf(stderr, "error: releasing CPUs: %s\n", __argv[4]);
		}
	}
	else if (!strcmp(__argv[3], "mem")) {
		ret = ioctl(fd, IHK_DEVICE_RELEASE_MEM, &req);

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
	char query_result[1024];

	if (__argc < 4) {
		usage(__argv);
		return -1;
	}

	memset(query_result, 0, sizeof(query_result));

	if (!strcmp(__argv[3], "cpu")) {
		ret = ioctl(fd, IHK_DEVICE_QUERY_CPU, query_result);

		if (ret != 0) {
			fprintf(stderr, "error: querying CPUs\n");
		}
	}
	else if (!strcmp(__argv[3], "mem")) {
		ret = ioctl(fd, IHK_DEVICE_QUERY_MEM, query_result);

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
