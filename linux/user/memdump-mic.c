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
	char fn[64];
	unsigned char buf[BPL];

	if (argc < 4) {
		fprintf(stderr, "Usage: %s (dev #) (offset) (length)\n", argv[0]);
		return 1;
	}
	n = atoi(argv[1]);
	offset = str_to_ul(argv[2]);
	len = str_to_ul(argv[3]);

	sprintf(fn, "/dev/mcd%d", n);
	fd = open(fn, O_RDONLY);

	if (fd == -1) {
		perror("open");
		return 1;
	}

	printf("Dump Offset : %016lx, Dump Length: %016lx\n", offset, len);
	printf("(Address)-------  "
	       "+0 +1 +2 +3 +4 +5 +6 +7 +8 +9 +a +b +c +d +e +f\n");

	while (len > 0) {
		printf("%016lx: ", offset);

		if ((n = pread(fd, buf, len > BPL ? BPL : len, offset)) <= 0) {
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
