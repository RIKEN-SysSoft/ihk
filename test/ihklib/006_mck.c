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
	printf("ihklib006_mck exit OK\n");
	return 0;
}
