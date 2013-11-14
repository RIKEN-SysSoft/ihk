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

static void usage(char **arg)
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
	fprintf(stderr, "    alloc [cpu [memory]]\n");
	fprintf(stderr, "    reserve_cpu [cpu_num]...\n");
	fprintf(stderr, "    reserve_mem (addr) (size)\n");
	fprintf(stderr, "    query\n");
	fprintf(stderr, "    query_free_mem\n");
	fprintf(stderr, "    kargs (kernel arg)\n");
	fprintf(stderr, "    kmsg\n");
	fprintf(stderr, "    clear_kmsg\n");
	fprintf(stderr, "    intr [intr]\n");
	fprintf(stderr, "    ioctl (req) (arg)\n");
}

static void do_boot(int fd)
{
	int r = ioctl(fd, IHK_OS_BOOT, 0);
	printf("ret = %d\n", r);
}

static void do_load(int fd)
{
	char *fn;
	if (__argc > 3) {
		fn = __argv[3];
	} else {
		fn = "/home/shimosawa/mcos/mcos.image";
	}
	int r = ioctl(fd, IHK_OS_LOAD, (unsigned long)fn);

	printf("ret = %d\n", r);
}

static void do_shutdown(int fd)
{
	int r = ioctl(fd, IHK_OS_SHUTDOWN, 0);
	printf("ret = %d\n", r);
}

static void do_alloc(int fd)
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
	printf("ret[cpu] = %d\n", r);

	r = ioctl(fd, IHK_OS_ALLOC_MEM, size);
	printf("ret[mem] = %d\n", r);
}

static void do_reserve_cpu(int fd)
{
	int i, n = __argc - 3, r;
	int *param;

	if (n <= 0) {
		printf("No CPU is specified.\n");
		return;
	}

	param = malloc(sizeof(int) * (n + 1));
	param[0] = n;
	for (i = 0; i < n; i++) {
		param[i + 1] = atoi(__argv[i + 3]);
	}

	r = ioctl(fd, IHK_OS_RESERVE_CPU, (unsigned long)param);
	printf("ret[cpu] = %d\n", r);
}

static void do_reserve_mem(int fd)
{
	int r;
	unsigned long arg[2];

	if (__argc <= 4) {
		printf("Start or size is not specified.\n");
		return;
	}
	arg[0] = strtol(__argv[3], NULL, 16);
	arg[1] = strtoll(__argv[4], NULL, 16);

	r = ioctl(fd, IHK_OS_RESERVE_MEM, (unsigned long)arg);
	printf("ret[mem] = %d\n", r);
}

static void do_query(int fd)
{
	int r = ioctl(fd, IHK_OS_QUERY_STATUS);
	printf("status = %d\n", r);
}

static void do_query_free_mem(int fd)
{
	int r = ioctl(fd, IHK_OS_QUERY_FREE_MEM);
	
	if (r < 0) {
		printf("error querying free memory\n");
	}

	printf("number of free pages (4kB): %d\n", r);
}


static void do_intr(int fd)
{
	int r;
	int v = 0xf1;
	if (__argc > 3) {
		v = atoi(__argv[3]);
	}
	r = ioctl(fd, IHK_OS_DEBUG_START, v);
	printf("ret = %d\n", r);
}

static void do_kargs(int fd)
{
	int r;

	if (__argc <= 3) {
		printf("No arg specified.\n");
		return;
	} else {
		r = ioctl(fd, IHK_OS_SET_KARGS, (char *)__argv[3]);
		printf("ret = %d\n", r);
	}
}

static void do_kmsg(int fd)
{
	char buf[16384];
	int r = ioctl(fd, IHK_OS_READ_KMSG, (unsigned long)buf);
	if (r >= 0) {
		buf[r] = 0;
		printf("%s\n", buf);
	} else {
		printf("error = %d\n", r);
	}
}

static void do_clear_kmsg(int fd)
{
	int r = ioctl(fd, IHK_OS_CLEAR_KMSG, 0);

	printf("ret = %d\n", r >= 0 ? r : -errno);
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
	printf("ret = %lx\n", r);
}

#define HANDLER(name) if (!strcmp(argv[2], #name)) { do_##name(fd); }
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
