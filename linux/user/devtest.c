#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include "ihk/ihk_host_user.h"
#include <sys/ioctl.h>
#include <string.h>

int __argc;
char **__argv;

static void do_destroy(int fd)
{
}



#define HANDLER(name) if (!strcmp(argv[2], #name)) { do_##name(fd); }
int main(int argc, char **argv)
{
	int fd;
	char fn[128];

	__argc = argc;
	__argv = argv;

	if (argc < 4) {
		fprintf(stderr, "Usage: %s (dev #) (action)\n", argv[0]);
		return 1;
	}

	sprintf(fn, "/dev/mcd%d", atoi(argv[1]));

	fd = open(fn, O_RDONLY);
	if (fd < 0) {
		perror("open");
		return 1;
	}

	HANDLER(read) 
	else HANDLER(mmap) 
	else {
		fprintf(stderr, "Unknown action : %s\n", argv[2]);
	}
	
	close(fd);
	return 0;
}
