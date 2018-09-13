/**
 * \file ihklib020_lin.c
 *  License details are found in the file LICENSE.
 * \brief
 *  Test ihk_os_get_assigned_cpus()
 * \author Masamichi Takagi  <masamichi.takagi@riken.jp> \par
 * Copyright (C) 2018  Masamichi Takagi
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ihklib.h>
#include <mckernel/ihklib_rusage.h>
#include <sys/types.h>
#include <errno.h>
#include <pwd.h>
#include "util.h"

int main(int argc, char **argv)
{
	int ret, status;
	FILE *fp;
	size_t nread;

	char cmd[1024];
	char logname[256], *envstr, *groups;

	int cpus[4];
	int num_cpus;

	int indices[2];
	int num_os_instances;

	char *retstr;
	struct passwd *pwd;

	fp = popen("logname", "r");
	nread = fread(logname, 1, sizeof(logname), fp);
	CHKANDJUMP(nread == 0, -1, "fread");
	retstr = strrchr(logname, '\n');
	if (retstr) {
		*retstr = 0;
	}
	printf("logname=%s\n", logname);

	envstr = getenv("GROUPS");
	CHKANDJUMP(envstr == NULL, -1, "groups");
	groups = strdup(envstr);
	retstr = strrchr(groups, '\n');
	if (retstr) {
		*retstr = 0;
	}
	printf("groups=%s\n", groups);

	if (geteuid() != 0) {
		printf("Execute as a root\n");
	}

	sprintf(cmd, "insmod %s/kmod/ihk.ko", QUOTE(MCK_DIR));
	status = system(cmd);
	CHKANDJUMP(WEXITSTATUS(status) != 0, -1, "system");

	sprintf(cmd, "insmod %s/kmod/ihk-smp-%s.ko "
		"ihk_start_irq=240 ihk_ikc_irq_core=0", QUOTE(MCK_DIR), QUOTE(ARCH));
	status = system(cmd);
	CHKANDJUMP(WEXITSTATUS(status) != 0, -1, "system");

	sprintf(cmd, "chown %s:%s /dev/mcd*\n", logname, groups);
	printf("%s\n", cmd);
	status = system(cmd);
	CHKANDJUMP(WEXITSTATUS(status) != 0, -1, "system");

	sprintf(cmd, "insmod %s/kmod/mcctrl.ko", QUOTE(MCK_DIR));
	status = system(cmd);
	CHKANDJUMP(WEXITSTATUS(status) != 0, -1, "system");

	// reserve cpu
	cpus[0] = 1;
	cpus[1] = 2;
	cpus[2] = 3;
	num_cpus = 3;
	ret = ihk_reserve_cpu(0, cpus, num_cpus);
	OKNG(ret == 0, "ihk_reserve_cpu 1,2,3 succeeded\n");

	// get # of reserved cpus
	num_cpus = ihk_get_num_reserved_cpus(0);
	OKNG(num_cpus == 3, "ihk_get_num_reserved_cpu returned 3\n");

	// get reserved cpus. Note that cpu# is sorted in ihk.
	ret = ihk_query_cpu(0, cpus, num_cpus);
	OKNG(ret == 0 &&
		 cpus[0] == 1 &&
		 cpus[1] == 2 &&
		 cpus[2] == 3, "ihk_query_cpu returned 1,2,3\n");

	// create 0
	ret = ihk_create_os(0);
	OKNG(ret == 0, "ihk_create_os succeeded\n");

	// get # of OS instances
	num_os_instances = ihk_get_num_os_instances(0);
	OKNG(num_os_instances == 1, "ihk_get_num_os_instances returned 1\n");

	/*
	 * get OS instances. Note that the index of the youngest OS
	 * instance resides in [0].
	 */
	ret = ihk_get_os_instances(0, indices, num_os_instances);
	OKNG(ret == 0 &&
		 indices[0] == 0, "ihk_get_os_instances returned index:0\n");

	// get status
	ret = ihk_os_get_status(0);
	OKNG(ret == IHK_STATUS_INACTIVE,
		"ihk_os_get_status returned IHK_STATUS_INACTIVE\n");

	sprintf(cmd, "chown %s:%s /dev/mcos*\n", logname, groups);
	status = system(cmd);
	CHKANDJUMP(WEXITSTATUS(status) != 0, -1, "system");

	// assign cpu 1,2,3
	num_cpus = 3;
	cpus[0] = 1;
	cpus[1] = 2;
	cpus[2] = 3;
	ret = ihk_os_assign_cpu(0, cpus, num_cpus);
	OKNG(ret == 0, "ihk_os_assign_cpu 1,2,3 succeeded\n");

	// get # of assigned cpus
	num_cpus = ihk_os_get_num_assigned_cpus(0);
	OKNG(num_cpus == 3, "ihk_os_get_num_assigned_cpus returned 3\n");

	// get assigned cpus
	ret = ihk_os_query_cpu(0, cpus, num_cpus);
	OKNG(ret == 0 &&
		 cpus[0] == 1 &&
		 cpus[1] == 2 &&
		 cpus[2] == 3, "ihk_os_query_cpu returned 1,2,3\n");

	// release cpu
	num_cpus = 3;
	cpus[0] = 1;
	cpus[1] = 2;
	cpus[2] = 3;
	ret = ihk_os_release_cpu(0, cpus, num_cpus);
	OKNG(ret == 0, "ihk_os_release_cpu 1,2,3 succeeded\n");

	// get # of assigned cpus
	num_cpus = ihk_os_get_num_assigned_cpus(0);
	OKNG(num_cpus == 0, "ihk_os_get_num_assigned_cpus returned 0\n");

	// assign cpu 2,3
	num_cpus = 2;
	cpus[0] = 2;
	cpus[1] = 3;
	ret = ihk_os_assign_cpu(0, cpus, num_cpus);
	OKNG(ret == 0, "ihk_os_assign_cpu 2,3 succeeded\n");

	// get # of assigned cpus
	num_cpus = ihk_os_get_num_assigned_cpus(0);
	OKNG(num_cpus == 2, "ihk_os_get_num_assigned_cpus returned 2\n");

	// get assigned cpus
	ret = ihk_os_query_cpu(0, cpus, num_cpus);
	OKNG(ret == 0 &&
		 cpus[0] == 2 &&
		 cpus[1] == 3, "ihk_os_query_cpu returned 2,3\n");

	// release cpu
	num_cpus = 1;
	cpus[0] = 3;
	ret = ihk_os_release_cpu(0, cpus, num_cpus);
	OKNG(ret == 0, "ihk_os_release_cpu 3 succeeded\n");

	// get # of assigned cpus
	num_cpus = ihk_os_get_num_assigned_cpus(0);
	OKNG(num_cpus == 1, "ihk_os_get_num_assigned_cpus returned 1\n");

	// get assigned cpus
	ret = ihk_os_query_cpu(0, cpus, num_cpus);
	OKNG(ret == 0 &&
		 cpus[0] == 2, "ihk_os_query_cpu returned 2\n");

	// release cpu
	num_cpus = 1;
	cpus[0] = 2;
	ret = ihk_os_release_cpu(0, cpus, num_cpus);
	OKNG(ret == 0, "ihk_os_release_cpu 2 succeeded\n");

	// get # of assigned cpus
	num_cpus = ihk_os_get_num_assigned_cpus(0);
	OKNG(num_cpus == 0, "ihk_os_get_num_assigned_cpus returned 0\n");

	// assign cpu 1,2
	num_cpus = 2;
	cpus[0] = 1;
	cpus[1] = 2;
	ret = ihk_os_assign_cpu(0, cpus, num_cpus);
	OKNG(ret == 0, "ihk_os_assign_cpu 1,2 succeeded\n");

	// get # of assigned cpus
	num_cpus = ihk_os_get_num_assigned_cpus(0);
	OKNG(num_cpus == 2, "ihk_os_get_num_assigned_cpus returned 2\n");

	// get assigned cpus
	ret = ihk_os_query_cpu(0, cpus, num_cpus);
	OKNG(ret == 0 &&
		 cpus[0] == 1 &&
		 cpus[1] == 2, "ihk_os_query_cpu returned 1,2\n");

	// get # of assigned cpus (error handling)
	ret = ihk_os_get_num_assigned_cpus(1);
	OKNG(ret == -ENOENT,
	     "ihk_os_get_num_assigned_cpus returned -ENOENT as expected\n");

	// get assigned cpus (error handling)
	ret = ihk_os_query_cpu(0, cpus, 1ULL<<30);
	OKNG(ret == -EINVAL,
	     "ihk_os_query_cpu returned -EINVAL as expected\n");

	// get assigned cpus (error handling)
	ret = ihk_os_query_cpu(1, cpus, num_cpus);
	OKNG(ret == -ENOENT,
	     "ihk_os_query_cpu returned -ENOENT as expected\n");

	// get assigned cpus (error handling)
	ret = ihk_os_query_cpu(0, cpus, num_cpus + 1);
	OKNG(ret == -EINVAL,
	     "ihk_os_query_cpu returned -EINVAL as expected\n");

	// seteuid bin
	CHKANDJUMP(geteuid(), -1, "not a superuser");

	CHKANDJUMP(!(pwd = getpwnam("bin")), -1,
		"getpwnam failed: %s\n", strerror(errno));

	CHKANDJUMP(seteuid(pwd->pw_uid), -1,
		 "seteuid failed: %s\n", strerror(errno));
	// get # of assigned cpus (error handling)
	ret = ihk_os_get_num_assigned_cpus(0);
	OKNG(ret == -EACCES,
	     "ihk_os_get_num_assigned_cpus returned -EACCES as expected\n");

	// get assigned cpus (error handling)
	ret = ihk_os_query_cpu(0, cpus, num_cpus);
	OKNG(ret == -EACCES,
	     "ihk_os_query_cpu returned -EACCES as expected\n");

	// setuid root
	CHKANDJUMP(seteuid(0), -1, "seteuid failed: %s\n", strerror(errno));

	// destroy OS
	ret = ihk_destroy_os(0, 0);
	OKNG(ret == 0, "ihk_destroy_os succeeded\n");

	// rmmod modules
	sprintf(cmd, "rmmod %s/kmod/mcctrl.ko", QUOTE(MCK_DIR));
	status = system(cmd);
	CHKANDJUMP(WEXITSTATUS(status) != 0, -1, "system");

	sprintf(cmd, "rmmod %s/kmod/ihk-smp-%s.ko", QUOTE(MCK_DIR), QUOTE(ARCH));
	status = system(cmd);
	CHKANDJUMP(WEXITSTATUS(status) != 0, -1, "system");

	sprintf(cmd, "rmmod %s/kmod/ihk.ko", QUOTE(MCK_DIR));
	status = system(cmd);
	CHKANDJUMP(WEXITSTATUS(status) != 0, -1, "system");

	printf("[INFO] All tests finished\n");
	ret = 0;
 fn_fail:
	return ret;
}
