#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <pthread.h>
#include <ihklib.h>
#include "util.h"

#define NTHR 2
#define NLOOP 10
#define NSTEAL 1
#define WAITFLAG (/*WNOHANG*/0)

struct thread_arg {
	int id;
	int ret;
};

struct thread_arg thr_arg[NTHR];
pthread_t thr[NTHR];
pthread_barrier_t bar1, bar2;


void *create_destroy_pseudofs(void *_arg)
{
	int ret;
	int i;
	int count = 0;

	pthread_barrier_wait(&bar1);

	for (i = 0; i < NLOOP; i++) {

		pthread_barrier_wait(&bar2);

		// create pseudofs
		ret = ihk_os_create_pseudofs(0, 0, 0);
		if (ret == -ECHILD) {
			printf("[INFO] ihk_os_create_pseudofs reported ECHILD\n");
			count++;
		} else {
			if (ret != 0) {
				eprintf("unexpected error in ihk_os_create_pseudofs: %s\n",
					strerror(-ret));
			}
		}

		pthread_barrier_wait(&bar2);

		// destroy pseudofs
		ret = ihk_os_destroy_pseudofs(0, 0, 0);
		if (ret == -ECHILD) {
			printf("[INFO] ihk_os_destroy_pseudofs reported ECHILD\n");
			count++;
		} else {
			if (ret != 0) {
				eprintf("unexpected error in ihk_os_destroy_pseudofs: %s\n",
					strerror(-ret));
			}
		}
	}

	OKNGNOJUMP(count > 0,
	     "create/destroy-thread reported ECHILD %d times\n",
	     count);

fn_fail:
	pthread_exit(0);
}

void *steal_child(void *_arg)
{
	int count = 0;
	int i, j;
	pid_t pid;
	int status;

	pthread_barrier_wait(&bar1);

	for (i = 0; i < NLOOP; i++) {
		pthread_barrier_wait(&bar2);

		for (j = 0; j < NSTEAL; j++) {
			pid = waitpid(-1, &status, WAITFLAG);
			if (pid > 0) {
				printf("[INFO] stole one child (%d)\n", pid);
				count++;
			}
		}
		pthread_barrier_wait(&bar2);

		for (j = 0; j < NSTEAL; j++) {
			pid = waitpid(-1, &status, WAITFLAG);
			if (pid > 0) {
				printf("[INFO] stole one child (%d)\n", pid);
				count++;
			}
		}
	}
	OKNGNOJUMP(count > 0, "steal-thread stole %d children\n", count);

 fn_fail:
	pthread_exit(0);
}

int main(int argc, char **argv)
{
	int ret, status;
	int i;
	FILE *fp;
	char buf[65536];
	size_t nread;

	char cmd[1024];
	char fn[256];
	char kargs[256];
	char logname[256], *envstr, *groups;

	int cpus[4];
	int num_cpus;

	struct ihk_mem_chunk mem_chunks[4];
	int num_mem_chunks;

	struct ihk_ikc_cpu_map ikc_map[2];

	char *retstr;

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

#if 0
	// turn off error messages
	ret = ihk_set_loglevel(IHKLIB_LOGLEVEL_EMERG);
	CHKANDJUMP(ret == -1, -1, "ihk_set_loglevel");
#endif
	// ihk_os_destroy_pseudofs
	ret = ihk_os_destroy_pseudofs(0, 0, 0);
	fp = popen("cat /proc/mounts | grep /tmp/mcos/mcos0_sys", "r");
	nread = fread(buf, 1, sizeof(buf), fp);
	buf[nread] = 0;
	OKNG(ret == 0 && strstr(buf, "/tmp/mcos/mcos0_sys") == NULL,
	     "ihk_os_destroy_pseudofs (1)\n");

	sprintf(cmd, "insmod %s/kmod/ihk.ko", QUOTE(MCK_DIR));
	status = system(cmd);
	CHKANDJUMP(WEXITSTATUS(status) != 0, -1, "system");

	sprintf(cmd,
		"insmod %s/kmod/ihk-smp-%s.ko ihk_start_irq=240 ihk_ikc_irq_core=0",
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
	num_cpus = 2;
	ret = ihk_reserve_cpu(0, cpus, num_cpus);
	OKNG(ret == 0, "ihk_reserve_cpu\n");

	// reserve mem 128m@0,64m@0
	num_mem_chunks = 2;
	mem_chunks[0].size = 128*1024*1024ULL;
	mem_chunks[0].numa_node_number = 0;
	mem_chunks[1].size = 64*1024*1024ULL;
	mem_chunks[1].numa_node_number = 0;
	ret = ihk_reserve_mem(0, mem_chunks, num_mem_chunks);
	OKNG(ret == 0, "ihk_reserve_mem\n");

	// get # of reserved mem chunks
	num_mem_chunks = ihk_get_num_reserved_mem_chunks(0);
	OKNG(num_mem_chunks == 2, "ihk_get_num_reserved_mem_chunks\n");

	// create 0
	ret = ihk_create_os(0);
	OKNG(ret == 0, "ihk_create_os\n");

	// chown /dev/mcos0
	sprintf(cmd, "chown %s:%s /dev/mcos*\n", logname, groups);
	status = system(cmd);
	CHKANDJUMP(WEXITSTATUS(status) != 0, -1, "system");

	// assign cpu
	num_cpus = 2;
	cpus[0] = 1;
	cpus[1] = 2;
	ret = ihk_os_assign_cpu(0, cpus, num_cpus);
	OKNG(ret == 0, "ihk_os_assign_cpu\n");

	// set ikc_map
	ikc_map[0].src_cpu = 1;
	ikc_map[0].dst_cpu = 0;
	ikc_map[1].src_cpu = 2;
	ikc_map[1].dst_cpu = 0;
	ret = ihk_os_set_ikc_map(0, ikc_map, num_cpus);
	OKNG(ret == 0, "ihk_os_set_ikc_map\n");

	// assign mem 128m@0,64m@0
	num_mem_chunks = 2;
	mem_chunks[0].size = 128*1024*1024ULL;
	mem_chunks[0].numa_node_number = 0;
	mem_chunks[1].size = 64*1024*1024ULL;
	mem_chunks[1].numa_node_number = 0;
	ret = ihk_os_assign_mem(0, mem_chunks, num_mem_chunks);
	OKNG(ret == 0, "ihk_os_assign_mem\n");

	// load
	sprintf(fn, "%s/%s/kernel/mckernel.img",
		QUOTE(MCK_DIR), QUOTE(TARGET));
	ret = ihk_os_load(0, fn);
	OKNG(ret == 0, "ihk_os_load\n");

	// kargs
	sprintf(kargs, "hidos ksyslogd=0");
	ret = ihk_os_kargs(0, kargs);
	OKNG(ret == 0, "ihk_os_kargs\n");

	// boot
	ret = ihk_os_boot(0);
	OKNG(ret == 0, "ihk_os_boot\n");

	// wait until OS gets ready
	while ((ret = ihk_os_get_status(0)) != IHK_STATUS_RUNNING) {
	}

	// test ihk_os_create_pseudofs
	pthread_barrier_init(&bar1, NULL, 3);
	pthread_barrier_init(&bar2, NULL, 2);

	ret = pthread_create(&thr[0], NULL,
			     create_destroy_pseudofs, &thr_arg[0]);
	CHKANDJUMP(ret == -1, -1, "pthread_create");

	ret = pthread_create(&thr[1], NULL,
			     steal_child, &thr_arg[1]);
	CHKANDJUMP(ret == -1, -1, "pthread_create");

	pthread_barrier_wait(&bar1);

	for (i = 0; i < NTHR; i++) {
		ret = pthread_join(thr[i], NULL);
		CHKANDJUMP(ret == -1, -1, "pthread_join");
	}

	// create pseudofs
	ret = ihk_os_create_pseudofs(0, 0, 0);
	fp = popen("cat /proc/mounts | grep /tmp/mcos/mcos0_sys",
		   "r");
	nread = fread(buf, 1, sizeof(buf), fp);
	buf[nread] = 0;
	OKNG(ret == 0 && strstr(buf, "/tmp/mcos/mcos0_sys") != NULL,
	     "ihk_os_create_pseudofs()\n");

	// mcexec
	sprintf(cmd, "%s/bin/mcexec ls -l | grep Makefile", QUOTE(MCK_DIR));
	fp = popen(cmd, "r");
	nread = fread(buf, 1, sizeof(buf), fp);
	buf[nread] = 0;
	OKNG(strstr(buf, "Makefile") != NULL, "mcexec\n");

	// shutdown
	ret = ihk_os_shutdown(0);
	OKNG(ret == 0, "ihk_os_shutdown\n");

	// wait until OS gets shutdown
	while ((ret = ihk_os_get_status(0)) != IHK_STATUS_INACTIVE) {
	}

	// destroy os
	ret = ihk_destroy_os(0, 0);
	OKNG(ret == 0, "ihk_destroy_os (4)\n");

	// rmmod mcctrl
	sprintf(cmd, "rmmod %s/kmod/mcctrl.ko", QUOTE(MCK_DIR));
	status = system(cmd);
	CHKANDJUMP(WEXITSTATUS(status) != 0, -1, "system");

	// destroy pseudofs
	ret = ihk_os_destroy_pseudofs(0, 0, 0);
	fp = popen("cat /proc/mounts | grep /tmp/mcos/mcos0_sys", "r");
	nread = fread(buf, 1, sizeof(buf), fp);
	buf[nread] = 0;
	OKNG(ret == 0 && strstr(buf, "/tmp/mcos/mcos0_sys") == NULL,
	     "ihk_os_destroy_pseudofs\n");

	// rmmod ihk-smp-x86.ko
	sprintf(cmd, "rmmod %s/kmod/ihk-smp-%s.ko",
		QUOTE(MCK_DIR), QUOTE(ARCH));
	status = system(cmd);
	CHKANDJUMP(WEXITSTATUS(status) != 0, -1,
		   "rmmod ihk-smp-x86 failed\n");

	sprintf(cmd, "rmmod %s/kmod/ihk.ko", QUOTE(MCK_DIR));
	status = system(cmd);
	CHKANDJUMP(WEXITSTATUS(status) != 0, -1, "system");

	printf("[INFO] All tests finished\n");

	ret = 0;

fn_fail:
	return ret;
}
