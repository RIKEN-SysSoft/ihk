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

int main(int argc, char **argv)
{
	int ret;

	/* Generate a lot of kmsg */
	ret = syscall(900);
	CHKANDJUMP(ret, 255, "syscall");
	printf("ihklib007_mck exit OK\n");
	ret = 0;

 fn_fail:
	return ret;
}
