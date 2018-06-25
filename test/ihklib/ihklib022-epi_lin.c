/**
 * \file ihklib020_lin.c
 *  License details are found in the file LICENSE.
 * \brief
 *  Test ihk_os_get_assigned_cpus()
 * \author Masamichi Takagi  <masamichi.takagi@riken.jp> \par
 * Copyright (C) 2018  Masamichi Takagi
 */

#define _GNU_SOURCE         /* See feature_test_macros(7) */
#include <unistd.h>
#include <sys/syscall.h>   /* For SYS_xxx definitions */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ihklib.h>
#include <mckernel/ihklib_rusage.h>
#include <sys/types.h>
#include <errno.h>
#include <linux/limits.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sched.h>
#include "util.h"

static char prefix[256];

int main(int argc, char **argv)
{
	int ret = 0;
	int status;
	FILE *fp, *fp1, *fp2;
	char buf[65536], buf2[65536];
	size_t nread;

	char cmd[1024];
	char fn[256];
	char kargs[256];
	char logname[256], *envstr, *dup, *line, *groups;

	int cpus[4];
	int num_cpus;

	struct ihk_mem_chunk mem_chunks[4];
	int num_mem_chunks;

	int indices[2];
	int num_os_instances;

	char *home;
	pid_t nspid;

	home = getenv("MYHOME");
	CHKANDJUMP(home == NULL, -1, "getenv");
	sprintf(prefix, "%s/project/os/install", home);

	// shutdown
	ret = ihk_os_shutdown(0);
	OKNG(ret == 0, "ihk_os_shutdown succeeded\n");

	// destroy OS
	ret = ihk_destroy_os(0, 0);
	OKNG(ret == 0, "ihk_destroy_os succeeded\n");

	// rmmod mcctrl
	sprintf(cmd, "rmmod %s/kmod/mcctrl.ko", prefix);
	status = system(cmd);
	CHKANDJUMP(WEXITSTATUS(status) != 0, -1, "system");

	// get pid of mount name space
	if (argc < 2) {
		CHKANDJUMP(1, -1, "specify pid in argument");
	}
	nspid = atoi(argv[1]);
	//printf("%s: nspid=%d\n", __FILE__, nspid);

	// destroy pseudofs
	ret = ihk_os_destroy_pseudofs(0, nspid, CLONE_NEWNS | CLONE_NEWPID);
	OKNG(ret == 0, "ihk_os_destroy_pseudofs succeeded\n");

	// rmmod ihk-smp-x86_64
	sprintf(cmd, "rmmod %s/kmod/ihk-smp-x86_64.ko", prefix);
	status = system(cmd);
	CHKANDJUMP(WEXITSTATUS(status) != 0, -1, "system");

	// rmmod ihk
	sprintf(cmd, "rmmod %s/kmod/ihk.ko", prefix);
	status = system(cmd);
	CHKANDJUMP(WEXITSTATUS(status) != 0, -1, "system");

	printf("All tests finished\n");
	ret = 0;
 fn_fail:
	return ret;
}
