#define _GNU_SOURCE	 /* See feature_test_macros(7) */
#include <string.h>
#include <sys/mman.h>

int main(int argc, char **argv)
{
	char *buf;

	while (1) {
		buf = mmap(0, 1UL << 30,
			   PROT_READ | PROT_WRITE,
			   MAP_ANONYMOUS | MAP_PRIVATE,
			   -1, 0);
		if (buf == MAP_FAILED) {
			continue;
		}
		memset(buf, 0xf3, 1UL << 30);
	}
	return 1;
}
