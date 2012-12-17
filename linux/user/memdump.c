#define _LARGEFILE64_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

unsigned long str_to_ul(char *str)
{
	unsigned long ret;

	if (!strncmp(str, "0x", 2)) {
		sscanf(str + 2, "%lx", &ret);
	}else{
		ret = atol(str);
	}

	return ret;
}

#define BPL 16

int main(int argc, char **argv)
{
	int i, n, fd;
	unsigned long offset, len;
	unsigned char buf[BPL];

	if (argc < 3) {
		fprintf(stderr, "Usage: %s [-k] (offset) (length)\n", argv[0]);
		return 1;
	}
	if (!strcmp(argv[1], "-k")) {
		offset = str_to_ul(argv[2]);
		len = str_to_ul(argv[3]);

		fd = open("/dev/kmem", O_RDONLY);
	} else {
		offset = str_to_ul(argv[1]);
		len = str_to_ul(argv[2]);
		fd = open("/dev/mem", O_RDONLY);
	}

	if (fd == -1) {
		perror("open");
		return 1;
	}

	if (lseek64(fd, offset, SEEK_SET) < 0) {
		perror("lseek");
		return 1;
	}
	
	printf("Dump Offset : %016lx, Dump Length: %016lx\n", offset, len);
	printf("(Address)-------  "
	       "+0 +1 +2 +3 +4 +5 +6 +7 +8 +9 +a +b +c +d +e +f\n");

	while (len > 0) {
		printf("%016lx: ", offset);

		if ((n = read(fd, buf, len > BPL ? BPL : len)) <= 0) {
			perror("read");
			break;
		}

		for (i = 0; i < n; i++){
			printf("%02x ", buf[i]);
		}

		printf("\n");

		len -= n;
		offset += n;
	}

	close(fd);

	return 0;
}
