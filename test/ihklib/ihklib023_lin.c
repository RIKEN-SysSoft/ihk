#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <errno.h>
#include <ihklib.h>
#include <mckernel/ihklib_rusage.h>
#include "util.h"

static char prefix[256];

#define NTHR 4

struct thread_arg {
	int id;
	int ret;
};

struct thread_arg thr_arg[NTHR];
pthread_t thr[NTHR];
pthread_barrier_t bar1, bar2;


void *ihk_query_device(void *_arg)
{
	struct thread_arg *arg = (struct thread_arg *)_arg;
	int ret;
	int i;
	int cpus[4];
	int num_cpus;
	struct ihk_mem_chunk mem_chunks[4];
	int num_mem_chunks;

	pthread_barrier_wait(&bar1);

	for (i = 0; i < 1000; i++) {
		// get # of reserved cpus
		while ((num_cpus = ihk_get_num_reserved_cpus(0)) == -EBUSY) {
		}
		NG(num_cpus == 3,
		   "thread #%d: ihk_get_num_reserved_cpu returned %d\n",
		   arg->id, num_cpus);

		// get reserved cpus. Note that cpu# is sorted in ihk.
		while ((ret = ihk_query_cpu(0, cpus, num_cpus)) == -EBUSY) {
		}
		NG(ret == 0 &&
		   cpus[0] == 1 &&
		   cpus[1] == 2 &&
		   cpus[2] == 3,
		   "thread #%d: ihk_query_cpu returned %d,%d,%d\n",
		   cpus[0], cpus[1], cpus[2]);

		// get # of reserved mem chunks
		while ((num_mem_chunks = ihk_get_num_reserved_mem_chunks(0)) ==
		       -EBUSY) {
		}
		NG(num_mem_chunks == 2,
		   "ihk_get_num_reserved_mem_chunks returned %d\n",
		   num_mem_chunks);

		// get reserved mem chunks
		while ((ret = ihk_query_mem(0, mem_chunks, num_mem_chunks)) ==
		       -EBUSY) {
		}
		NG(ret == 0 &&
		     (mem_chunks[0].size == 128*1024*1024ULL &&
		      mem_chunks[0].numa_node_number == 0 &&
		      mem_chunks[1].size == 64*1024*1024ULL &&
		      mem_chunks[1].numa_node_number == 0) ||
		     (mem_chunks[0].size == 64*1024*1024ULL &&
		      mem_chunks[0].numa_node_number == 0 &&
		      mem_chunks[1].size == 128*1024*1024ULL &&
		      mem_chunks[1].numa_node_number == 0),
		   "ihk_query_mem returned %ld@%d,%ld@%d\n",
		   mem_chunks[0].size, mem_chunks[0].numa_node_number,
		   mem_chunks[1].size, mem_chunks[1].numa_node_number);
	}

	ret = 0;
fn_fail:
	arg->ret = ret;
	pthread_exit(0);
}

void *ihk_query_os(void *_arg)
{
	struct thread_arg *arg = (struct thread_arg *)_arg;
	int ret;
	int i;
	int cpus[4];
	int num_cpus;
	struct ihk_mem_chunk mem_chunks[4];
	int num_mem_chunks;
	struct ihk_ikc_cpu_map ikc_map[2];

	pthread_barrier_wait(&bar2);

	for (i = 0; i < 1000; i++) {

		// get # of assigned cpus
		num_cpus = ihk_os_get_num_assigned_cpus(0);
		NG(num_cpus == 3,
		   "ihk_os_get_num_assigned_cpus returned %d\n",
		   num_cpus);

		// get assigned cpus
		ret = ihk_os_query_cpu(0, cpus, num_cpus);
		NG(ret == 0 &&
		     cpus[0] == 1 &&
		     cpus[1] == 2 &&
		     cpus[2] == 3,
		   "ihk_os_query_cpu returnded %d,%d,%d\n",
		   cpus[0], cpus[1], cpus[2]);

		// get ikc_map
		ret = ihk_os_get_ikc_map(0, ikc_map, num_cpus);
		NG(ret == 0 &&
		     ikc_map[0].src_cpu == 1 &&
		     ikc_map[0].dst_cpu == 0 &&
		     ikc_map[1].src_cpu == 2 &&
		     ikc_map[1].dst_cpu == 0 &&
		     ikc_map[2].src_cpu == 3 &&
		     ikc_map[2].dst_cpu == 0,
		   "ihk_os_get_ikc_map returned %d:%d,%d:%d,%d:%d\n",
		   ikc_map[0].src_cpu, ikc_map[0].dst_cpu,
		   ikc_map[1].src_cpu, ikc_map[1].dst_cpu,
		   ikc_map[2].src_cpu, ikc_map[2].dst_cpu
		   );

		// get # of assigned mem chunks
		num_mem_chunks = ihk_os_get_num_assigned_mem_chunks(0);
		NG(num_mem_chunks == 2,
		   "ihk_os_get_num_assigned_mem_chunks returned %d\n",
		   num_mem_chunks);

		// get assigned mem chunks
		ret = ihk_os_query_mem(0, mem_chunks, num_mem_chunks);
		NG(ret == 0 &&
		     (mem_chunks[0].size == 128*1024*1024ULL &&
		      mem_chunks[0].numa_node_number == 0 &&
		      mem_chunks[1].size == 64*1024*1024ULL &&
		      mem_chunks[1].numa_node_number == 0) ||
		     (mem_chunks[0].size == 64*1024*1024ULL &&
		      mem_chunks[0].numa_node_number == 0 &&
		      mem_chunks[1].size == 128*1024*1024ULL &&
		      mem_chunks[1].numa_node_number == 0),
		   "ihk_os_query_mem returned %ld@%d,%ld@%d\n",
		   mem_chunks[0].size, mem_chunks[0].numa_node_number,
		   mem_chunks[1].size, mem_chunks[1].numa_node_number);
	}

	ret = 0;
fn_fail:
	arg->ret = ret;
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

	int indices[2];
	int num_os_instances;

	struct ihk_ikc_cpu_map ikc_map[2];

	char *home, *retstr;

	home = getenv("MYHOME");
	CHKANDJUMP(home == NULL, -1, "getenv");
	sprintf(prefix, "%s/project/os/install", home);

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
		printf("Execute as a root like: sudo bash -c 'LD_LIBRARY_PATH=%s/lib/ %s'",
		       argv[0], prefix);
	}

	// ihk_os_destroy_pseudofs
	ret = ihk_os_destroy_pseudofs(0, 0, 0);
	fp = popen("cat /proc/mounts | grep /tmp/mcos/mcos0_sys", "r");
	nread = fread(buf, 1, sizeof(buf), fp);
	buf[nread] = 0;
	OKNG(ret == 0 && strstr(buf, "/tmp/mcos/mcos0_sys") == NULL,
	     "ihk_os_destroy_pseudofs (1)\n");

	sprintf(cmd, "insmod %s/kmod/ihk.ko", prefix);
	status = system(cmd);
	CHKANDJUMP(WEXITSTATUS(status) != 0, -1, "system");

	sprintf(cmd, "insmod %s/kmod/ihk-smp-x86_64.ko ihk_start_irq=240 ihk_ikc_irq_core=0",
		prefix);
	status = system(cmd);
	CHKANDJUMP(WEXITSTATUS(status) != 0, -1, "system");

	sprintf(cmd, "chown %s:%s /dev/mcd*\n", logname, groups);
	status = system(cmd);
	CHKANDJUMP(WEXITSTATUS(status) != 0, -1, "system");

	sprintf(cmd, "insmod %s/kmod/mcctrl.ko", prefix);
	status = system(cmd);
	CHKANDJUMP(WEXITSTATUS(status) != 0, -1, "system");

	// reserve cpu
	cpus[0] = 1;
	cpus[1] = 2;
	cpus[2] = 3;
	num_cpus = 3;
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

	// create threads to call ihklib functions concurrently
	pthread_barrier_init(&bar1, NULL, NTHR + 1);

	for (i = 0; i < NTHR; i++) {
		thr_arg[i].id = i;
		ret = pthread_create(&thr[i], NULL,
				     ihk_query_device, &thr_arg[i]);
		CHKANDJUMP(ret == -1, -1, "pthread_create");
	}

	pthread_barrier_wait(&bar1);

	for (i = 0; i < NTHR; i++) {
		ret = pthread_join(thr[i], NULL);
		CHKANDJUMP(ret == -1, -1, "pthread_join");
	}

	// create 0
	ret = ihk_create_os(0);
	OKNG(ret == 0, "ihk_create_os\n");

	// chown /dev/mcos0
	sprintf(cmd, "chown %s:%s /dev/mcos*\n", logname, groups);
	status = system(cmd);
	CHKANDJUMP(WEXITSTATUS(status) != 0, -1, "system");

	// assign cpu
	num_cpus = 3;
	cpus[0] = 1;
	cpus[1] = 2;
	cpus[2] = 3;
	ret = ihk_os_assign_cpu(0, cpus, num_cpus);
	OKNG(ret == 0, "ihk_os_assign_cpu\n");

	// set ikc_map
	ikc_map[0].src_cpu = 1;
	ikc_map[0].dst_cpu = 0;
	ikc_map[1].src_cpu = 2;
	ikc_map[1].dst_cpu = 0;
	ikc_map[2].src_cpu = 3;
	ikc_map[2].dst_cpu = 0;
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
	sprintf(fn, "%s/smp-x86/kernel/mckernel.img", prefix);
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

	// create pseudofs
	ret = ihk_os_create_pseudofs(0, 0, 0);
	fp = popen("cat /proc/mounts | grep /tmp/mcos/mcos0_sys",
		   "r");
	nread = fread(buf, 1, sizeof(buf), fp);
	buf[nread] = 0;
	OKNG(ret == 0 && strstr(buf, "/tmp/mcos/mcos0_sys") != NULL,
	     "ihk_os_create_pseudofs()\n");

	// create threads to call ihklib functions concurrently
	pthread_barrier_init(&bar2, NULL, NTHR + 1);

	for (i = 0; i < NTHR; i++) {
		thr_arg[i].id = i;
		ret = pthread_create(&thr[i], NULL,
				     ihk_query_os, &thr_arg[i]);
		CHKANDJUMP(ret == -1, -1, "pthread_create");
	}

	pthread_barrier_wait(&bar2);

	for (i = 0; i < NTHR; i++) {
		ret = pthread_join(thr[i], NULL);
		CHKANDJUMP(ret == -1, -1, "pthread_join");
	}

	// mcexec
	sprintf(cmd, "%s/bin/mcexec ls -l | grep Makefile", prefix);
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
	sprintf(cmd, "rmmod %s/kmod/mcctrl.ko", prefix);
	status = system(cmd);
	CHKANDJUMP(WEXITSTATUS(status) != 0, -1, "system");

	// destroy pseudofs
	ret = ihk_os_destroy_pseudofs(0, 0, 0);
	fp = popen("cat /proc/mounts | grep /tmp/mcos/mcos0_sys", "r");
	nread = fread(buf, 1, sizeof(buf), fp);
	buf[nread] = 0;
	OKNG(ret == 0 && strstr(buf, "/tmp/mcos/mcos0_sys") == NULL,
	     "ihk_os_destroy_pseudofs\n");

	// rmmod ihk[-smp-x86_64].ko
	sprintf(cmd, "rmmod %s/kmod/ihk-smp-x86_64.ko", prefix);
	status = system(cmd);
	CHKANDJUMP(WEXITSTATUS(status) != 0, -1, "system");

	sprintf(cmd, "rmmod %s/kmod/ihk.ko", prefix);
	status = system(cmd);
	CHKANDJUMP(WEXITSTATUS(status) != 0, -1, "system");

	printf("All tests finished\n");

	ret = 0;
fn_fail:
	return ret;
}
