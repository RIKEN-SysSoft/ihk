#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include "ihk/ihk_host_user.h"
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>

int __argc;
char **__argv;

static void do_destroy(int fd)
{
}

static void do_read(int fd)
{
	unsigned long adr;
	unsigned char buf[16];
	int i;

	if (__argc > 3) {
		adr = strtol(__argv[3], NULL, 16);
	} else {
		fprintf(stderr, "Address is not specified!\n");
		return;
	}
	
	i = pread(fd, buf, sizeof(buf), adr);
	if (i < 0) {
		perror("pread");
		return;
	}

	for (i = 0; i < sizeof(buf); i++) {
		printf("%02x ", buf[i]);
	}
	printf("\n");
}

static void do_mmap(int fd)
{
	unsigned long adr;
	unsigned char *p;
	int i;

	if (__argc > 3) {
		adr = strtol(__argv[3], NULL, 16);
	} else {
		fprintf(stderr, "Address is not specified!\n");
		return;
	}

	p = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, adr);
	if (p == MAP_FAILED) {
		perror("mmap");
		return;
	}

	for (i = 0; i < 16; i++) {
		printf("%02x ", p[i]);
	}
	printf("\n");

	munmap(p, 4096);
}

static void do_clear_kmsg(int fd)
{
	unsigned long adr;
	unsigned char *p;
	int i;

	if (__argc > 3) {
		adr = strtol(__argv[3], NULL, 16);
	} else {
		fprintf(stderr, "Address is not specified!\n");
		return;
	}

	p = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, adr);
	if (p == MAP_FAILED) {
		perror("mmap");
		return;
	}

	printf("Before write : %d\n", *(unsigned int *)p);
	*(unsigned int *)p = 0;
	munmap(p, 4096);
}

static void do_clear_kmsg_write(int fd)
{
	unsigned long adr;
	unsigned int l;
	int i;

	if (__argc > 3) {
		adr = strtol(__argv[3], NULL, 16);
	} else {
		fprintf(stderr, "Address is not specified!\n");
		return;
	}
	
	l = 0;
	i = pwrite(fd, &l, sizeof(l), adr);
	if (i < 0) {
		perror("pwrite");
		return;
	}
}

static void do_create(int fd)
{
	int r = ioctl(fd, AAL_DEVICE_CREATE_OS, 0);
	printf("ret = %d\n", r);
}
static void do_scratch(int fd)
{
	int i;
	long r;

	for (i = 0; i < 16; i++) {
		r = ioctl(fd, AAL_DEVICE_DEBUG_START + 0, i);
		printf("Scratch %2d = %08lx\n", i, r);
	}
}
static void do_sbox(int fd)
{
	int idx;
	long r;

	if (__argc > 3) {
		idx = strtol(__argv[3], NULL, 16);
	} else {
		idx = 0x1030;
	}

	r = ioctl(fd, AAL_DEVICE_DEBUG_START + 1, idx);
	printf("SBOX %04x = %08lx\n", idx, r);
}

static void do_ioctl(int fd)
{
	unsigned int req;
	unsigned long arg;
	long r;

	if (__argc <= 4) {
		fprintf(stderr, "No req or arg is specified.\n");
		return;
	}
	req = strtol(__argv[3], NULL, 16);
	arg = strtoll(__argv[4], NULL, 16);

	r = ioctl(fd, req, arg);
	printf("ret = %lx (%ld)\n", r, r);
}

#define HANDLER(name) if (!strcmp(argv[2], #name)) { do_##name(fd); }
int main(int argc, char **argv)
{
	int fd;
	char fn[128];

	__argc = argc;
	__argv = argv;

	if (argc < 3) {
		fprintf(stderr, "Usage: %s (dev #) (action)\n", argv[0]);
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
	else {
		fprintf(stderr, "Unknown action : %s\n", argv[2]);
	}
	
	close(fd);
	return 0;
}
