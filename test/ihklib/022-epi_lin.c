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
#include <sys/types.h>
#include <errno.h>
#include <linux/limits.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sched.h>
#include "util.h"

int main(int argc, char **argv)
{
	int ret, status;
	char cmd[1024];
	pid_t nspid;

	// shutdown
	ret = ihk_os_shutdown(0);
	OKNG(ret == 0, "ihk_os_shutdown succeeded\n");

	// destroy OS
	ret = ihk_destroy_os(0, 0);
	OKNG(ret == 0, "ihk_destroy_os succeeded\n");

	// rmmod mcctrl
	sprintf(cmd, "rmmod %s/kmod/mcctrl.ko", QUOTE(MCK_DIR));
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

	// rmmod ihk-smp-x86
	sprintf(cmd, "rmmod %s/kmod/ihk-smp-%s.ko",
		QUOTE(MCK_DIR), QUOTE(ARCH));
	status = system(cmd);
	CHKANDJUMP(WEXITSTATUS(status) != 0, -1,
		   "rmmod ihk-smp-x86 failed\n");

	// rmmod ihk
	sprintf(cmd, "rmmod %s/kmod/ihk.ko", QUOTE(MCK_DIR));
	status = system(cmd);
	CHKANDJUMP(WEXITSTATUS(status) != 0, -1, "system");

	printf("[INFO] All tests finished\n");
	ret = 0;

 fn_fail:
	return ret;
}
