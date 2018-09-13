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

	daemon(1, 1);

	/* Make McKernel hang up */
	ret = syscall(900);
	CHKANDJUMP(1, 255, "syscall\n");

 fn_exit:
	return ret;
 fn_fail:
	goto fn_exit;
}
