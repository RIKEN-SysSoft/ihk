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

	char *prefix = QUOTE(MCK_DIR);
	char cmd[1024];
	char fn[256];
	char kargs[256];

	int cpus[3] = {12, 13, 14};
	int num_cpus = 3;

	struct ihk_mem_chunk mem_chunks[4];
	int num_mem_chunks;

	struct ihk_perf_event_attr perfattr[1] = { 0 };

	printf("*** test start *************************\n");

	sprintf(cmd, "%s/sbin/mcstop+release.sh", prefix);
	status = system(cmd);
	NG(status == 0, "mcstop+release.sh");

	sprintf(cmd, "taskset -c 0 insmod %s/kmod/ihk.ko", prefix);
	status = system(cmd);
	NG(status == 0, "insmod ihk.ko");

	sprintf(cmd, "taskset -c 0 insmod %s/kmod/ihk-smp-arm64.ko ihk_ikc_irq_core=0", prefix);
	status = system(cmd);
	NG(status == 0, "insmod ihk-smp-arm64.ko");

	// reserve mem 1G@3,1G@4
	num_mem_chunks = 2;
	mem_chunks[0].size = (1UL << 30);
	mem_chunks[0].numa_node_number = 0;
	mem_chunks[1].size = (1UL << 30);
	mem_chunks[1].numa_node_number = 1;
	ret = ihk_reserve_mem(0, mem_chunks, num_mem_chunks);
	OKNG(ret == 0, "ihk_reserve_mem");

	// reserve cpus
	ret = ihk_reserve_cpu(0, cpus, num_cpus);
	OKNG(ret == 0, "ihk_reserve_cpu");

	sprintf(cmd, "taskset -c 0 insmod %s/kmod/mcctrl.ko", prefix);
	status = system(cmd);
	NG(status == 0, "insmod mcctrl.ko");

	// create 0
	ret = ihk_create_os(0);
	OKNG(ret == 0, "ihk_create_os");

	// assign cpus
	ret = ihk_os_assign_cpu(0, cpus, num_cpus);
	OKNG(ret == 0, "ihk_os_assign_cpu");

	// assign mem 1G@3,1G@4
	ret = ihk_os_assign_mem(0, mem_chunks, num_mem_chunks);
	OKNG(ret == 0, "ihk_os_assign_mem (2)");

	// load
	sprintf(fn, "%s/smp-arm64/kernel/mckernel.img", prefix);
	ret = ihk_os_load(0, fn);
	OKNG(ret == 0, "ihk_os_load");

	// kargs
	sprintf(kargs, "hidos dump_level=24 time_sharing");
	ret = ihk_os_kargs(0, kargs);
	OKNG(ret == 0, "ihk_os_kargs");

	// boot
	ret = ihk_os_boot(0);
	OKNG(ret == 0, "ihk_os_boot");

	// kmsg
	sprintf(cmd, "%s/sbin/ihkosctl 0 kmsg", prefix);
	status = system(cmd);
	OKNG(status == 0, "rmmod mcctrl");

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
