#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ihklib.h>
#include <sys/types.h>
#include <errno.h>

#define Q(x) #x
#define QUOTE(x) Q(x)

#define _OKNG(verb, cond, fmt, args...) do {                     \
	if (cond) {                                              \
		if (verb)                                        \
			printf("[OK] " fmt "\n", ##args);        \
	} else {                                                 \
		printf("[NG] " fmt ": %d\n", ##args, ret);       \
		goto out;					 \
	}                                                        \
} while (0)

#define OKNG(args...) _OKNG(1, ##args)
#define NG(args...) _OKNG(0, ##args)

int main(int argc, char **argv)
{
	int ret = 0, status, i;
	FILE *fp;
	char buf[65536];
	size_t nread;

	char *prefix_usr = QUOTE(PREFIX_USR);
	char *prefix_modules = QUOTE(PREFIX_MODULES);
	char *prefix_images = QUOTE(PREFIX_IMAGES);
	char cmd[1024];
	char fn[256];
	char kargs[256];

	int cpus[3] = {12, 13, 14};
	int num_cpus = 3;

	struct ihk_mem_chunk mem_chunks[4];
	int num_mem_chunks;

	struct ihk_perf_event_attr perfattr[1] = { 0 };

	printf("*** test start *************************\n");

	sprintf(cmd, "taskset -c 0 insmod %s/ihk.ko", prefix_modules);
	status = system(cmd);
	NG(status == 0, "insmod ihk.ko");

	sprintf(cmd, "taskset -c 0 insmod %s/ihk-smp-arm64.ko ihk_ikc_irq_core=0", prefix_modules);
	status = system(cmd);
	NG(status == 0, "insmod ihk-smp-arm64.ko");

	// reserve mem 1G@4,1G@5
	num_mem_chunks = 2;
	mem_chunks[0].size = (1UL << 30);
	mem_chunks[0].numa_node_number = 4;
	mem_chunks[1].size = (1UL << 30);
	mem_chunks[1].numa_node_number = 5;
	ret = ihk_reserve_mem(0, mem_chunks, num_mem_chunks);
	OKNG(ret == 0, "ihk_reserve_mem");

	// reserve cpus
	ret = ihk_reserve_cpu(0, cpus, num_cpus);
	OKNG(ret == 0, "ihk_reserve_cpu");

	sprintf(cmd, "taskset -c 0 insmod %s/mcctrl.ko", prefix_modules);
	status = system(cmd);
	NG(status == 0, "insmod mcctrl.ko");

	// create 0
	ret = ihk_create_os(0);
	OKNG(ret == 0, "ihk_create_os");

	// assign cpus
	ret = ihk_os_assign_cpu(0, cpus, num_cpus);
	OKNG(ret == 0, "ihk_os_assign_cpu");

	// assign mem
	ret = ihk_os_assign_mem(0, mem_chunks, num_mem_chunks);
	OKNG(ret == 0, "ihk_os_assign_mem");

	// load
	sprintf(fn, "%s/mckernel.img", prefix_images);
	ret = ihk_os_load(0, fn);
	OKNG(ret == 0, "ihk_os_load");

	// kargs
	sprintf(kargs, "hidos dump_level=24 time_sharing");
	ret = ihk_os_kargs(0, kargs);
	OKNG(ret == 0, "ihk_os_kargs");

	// boot
	ret = ihk_os_boot(0);
	OKNG(ret == 0, "ihk_os_boot");

	// status
	ret = ihk_os_get_status(0);
	for (i = 0; i < 5; i++) {
		ret = ihk_os_get_status(0);
		if (ret == IHK_STATUS_RUNNING) {
			goto status_ok;
		}
		usleep(1000000);
	}
	
	OKNG(0, "ihk_os_get_status");
	goto status_skip;
 status_ok:
	OKNG(1, "ihk_os_get_status");
 status_skip:

	// mcexec numactl
	sprintf(cmd, "%s/bin/mcexec numactl -H", prefix_usr);
	status = system(cmd);
	OKNG(status == 0, "mcexec numactl");

	// kmsg
	sprintf(cmd, "%s/sbin/ihkosctl 0 kmsg", prefix_usr);
	status = system(cmd);
	OKNG(status == 0, "ihkosctl 0 kmsg");

	ret = ihk_destroy_os(0, 0);
	OKNG(ret == 0, "destroy");

	// release cpus
	ret = ihk_release_cpu(0, cpus, num_cpus);
	OKNG(ret == 0, "ihk_release_cpu");

	// release mem all
	num_mem_chunks = 1;
	mem_chunks[0].size = (unsigned long)-1;
	mem_chunks[0].numa_node_number = 0;
	ret = ihk_release_mem(0, mem_chunks, num_mem_chunks);
	OKNG(ret == 0, "ihk_release_mem");

	sprintf(cmd, "rmmod mcctrl");
	status = system(cmd);
	OKNG(status == 0, "rmmod mcctrl");

	sprintf(cmd, "rmmod ihk_smp_arm64");
	status = system(cmd);
	NG(status == 0, "rmmod ihk_smp_arm64");

	sprintf(cmd, "rmmod ihk");
	status = system(cmd);
	NG(status == 0, "rmmod ihk");

	printf("*** All tests finished\n");

	ret = 0;
out:
	return ret;
}
