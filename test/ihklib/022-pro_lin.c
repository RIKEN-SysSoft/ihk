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
	FILE *fp;
	char buf[65536], buf2[65536];
	size_t nread;

	char cmd[1024];
	char fn[256];
	char kargs[256];
	char logname[256], *envstr, *groups;

	int cpus[4];
	int num_cpus;

	struct ihk_mem_chunk mem_chunks[4];
	int num_mem_chunks;

	int indices[2];
	int num_os_instances;

	char *retstr;
	pid_t nspid;

	fp = popen("logname", "r");
	nread = fread(logname, 1, sizeof(logname), fp);
	CHKANDJUMP(nread == 0, -1, "fread");
	retstr = strrchr(logname, '\n');
	if (retstr) {
		*retstr = 0;
	}

	envstr = getenv("MYGROUPS");
	CHKANDJUMP(envstr == NULL, -1, "groups");
	groups = strdup(envstr);
	retstr = strrchr(groups, '\n');
	if (retstr) {
		*retstr = 0;
	}

	if (geteuid() != 0) {
		printf("Execute as a root\n");
	}

	/* insmod */
	sprintf(cmd, "insmod %s/kmod/ihk.ko", QUOTE(MCK_DIR));
	status = system(cmd);
	CHKANDJUMP(WEXITSTATUS(status) != 0, -1, "system");

	sprintf(cmd, "insmod %s/kmod/ihk-smp-%s.ko "
		"ihk_start_irq=240 ihk_ikc_irq_core=0",
		QUOTE(MCK_DIR), QUOTE(ARCH));
	status = system(cmd);
	CHKANDJUMP(WEXITSTATUS(status) != 0, -1, "system");

	sprintf(cmd, "chown %s:%s /dev/mcd*\n", logname, groups);
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

	// reserve mem 512m@0
	num_mem_chunks = 1;
	mem_chunks[0].size = 512*1024*1024ULL;
	mem_chunks[0].numa_node_number = 0;
	ret = ihk_reserve_mem(0, mem_chunks, num_mem_chunks);
	OKNG(ret == 0, "ihk_reserve_mem 512M@0 succeeded\n");

	// create 0
	ret = ihk_create_os(0);
	OKNG(ret == 0, "ihk_create_os succeeded\n");

	// get # of OS instances
	num_os_instances = ihk_get_num_os_instances(0);
	OKNG(num_os_instances == 1, "ihk_get_num_os_instances returned 1\n");

	// get OS instances
	ret = ihk_get_os_instances(0, indices, num_os_instances);
	OKNG(ret == 0 &&
		 indices[0] == 0, "ihk_get_os_instances returned index:0\n");

	// get status
	ret = ihk_os_get_status(0);
	OKNG(ret == IHK_STATUS_INACTIVE,
		"ihk_os_get_status returned IHK_STATUS_INACTIVE\n");

	// chown /dev/mcos0
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

	// assign mem 512m@0
	num_mem_chunks = 1;
	mem_chunks[0].size = 512*1024*1024ULL;
	mem_chunks[0].numa_node_number = 0;
	ret = ihk_os_assign_mem(0, mem_chunks, num_mem_chunks);
	OKNG(ret == 0, "ihk_os_assign_mem 512M@0 succeeded\n");

	// load
	sprintf(fn, "%s/%s/kernel/mckernel.img",
		QUOTE(MCK_DIR), QUOTE(TARGET));
	ret = ihk_os_load(0, fn);
	OKNG(ret == 0, "ihk_os_load succeeded\n");

	// kargs
	sprintf(kargs, "hidos ksyslogd=0");
	ret = ihk_os_kargs(0, kargs);
	OKNG(ret == 0, "ihk_os_kargs succeeded\n");

	// boot
	ret = ihk_os_boot(0);
	OKNG(ret == 0, "ihk_os_boot succeeded\n");

	// wait until it's ready
	while ((ret = ihk_os_get_status(0)) != IHK_STATUS_RUNNING) {
	}
	OKNG(1, "ihk_os_get_status returned IHK_STATUS_RUNNING\n");

	// get pid of mount name space
	if (argc < 2) {
		CHKANDJUMP(1, -1, "specify pid in argument");
	}
	nspid = atoi(argv[1]);
	//printf("%s: nspid=%d\n", __FILE__, nspid);

	// overlay the McKernel /proc over the restricted Linux /proc
	ret = ihk_os_create_pseudofs(0, nspid, CLONE_NEWNS | CLONE_NEWPID);
	OKNG(ret == 0, "ihk_os_create_pseudofs succeeded\n");

	// job-scheduler can see the unrestricted Linux /proc
	sprintf(cmd, "cat /proc/1/cmdline > ./cmdline.global");
	status = system(cmd);

	fp = fopen("./cmdline.global", "r");
	nread = fread(buf, 1, sizeof(buf), fp);
	buf[nread] = 0;
	printf("%s: contents of /proc/1/cmdline in "
		"namespace of job-scheduler: %s\n", __FILE__, buf);
	// containerized process can't see the unrestricted Linux /proc
	sprintf(cmd, "nsenter -t %d -m -p "
		"%s/bin/mcexec cat /proc/1/cmdline > ./cmdline.local",
		nspid, QUOTE(MCK_DIR));
	status = system(cmd);

	fp = fopen("./cmdline.local", "r");
	nread = fread(buf2, 1, sizeof(buf2), fp);
	buf2[nread] = 0;
	printf("%s: contents of /proc/1/cmdline in "
		"namespace of pid %d: %s\n", __FILE__, nspid, buf2);

	OKNG(strcmp(buf, buf2) != 0,
		"job sees different /proc than job-scheduler\n");

	ret = 0;

 fn_fail:
	return ret;
}
