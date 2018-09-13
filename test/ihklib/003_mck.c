#define _GNU_SOURCE         /* See feature_test_macros(7) */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <string.h>
#include <sys/types.h>
#include <sys/mman.h>
#include "util.h"

#define DEBUG

int sz_mem[] = {
	4 * (1ULL<<10),
	2 * (1ULL<<20),
	1 * (1ULL<<30)
};
#define SZ_INDEX 1

int main(int argc, char **argv)
{
	int ret = 0;
	void *mem;
	int i;

	for (i = 0; i < (256 * (1ULL<<20)) / sz_mem[SZ_INDEX]; i++) {
		mem = mmap(0, sz_mem[SZ_INDEX], PROT_READ | PROT_WRITE,
			   MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
		CHKANDJUMP(mem == MAP_FAILED, 255, "mmap failed\n");
		memset(mem, 1, sz_mem[SZ_INDEX]);
	}

 fn_fail:
	return ret;
}
