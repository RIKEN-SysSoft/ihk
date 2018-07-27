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
		char buf[65536];                                 \
		char cmd[256];                                   \
		sprintf(cmd, "%s/sbin/ihkosctl 0 kmsg", prefix); \
		FILE *fp = popen(cmd, "r");                      \
		size_t nread = fread(buf, 1, sizeof(buf), fp);   \
		buf[nread] = 0;                                  \
		printf("%s", buf);                               \
		goto fn_fail;                                    \
	}                                                        \
} while (0)

#define OKNG(args...) _OKNG(1, ##args)
#define NG(args...) _OKNG(0, ##args)

int main(int argc, char **argv)
{
	int ret = 0, status, i, loop = 0;
	FILE *fp;
	char buf[65536];
	size_t nread;

	char *prefix = QUOTE(MCKDIR);
	char cmd[1024];
	char fn[256];
	char kargs[256];

	int cpus[3] = {1, 2, 3};
	int num_cpus = 3;

	struct ihk_mem_chunk mem_chunks[4];
	int num_mem_chunks;

	struct ihk_perf_event_attr perfattr[1] = { 0 };

	if (argc > 1)
		prefix = argv[1];

	printf("*** test start *************************\n");
	/*--------------------------------------------
	 * Preparing
	 *--------------------------------------------
	 */
	sprintf(cmd, "%s/sbin/mcstop+release.sh", prefix);
	status = system(cmd);
	NG(status == 0, "mcstop+release.sh");

	// ihk_os_destroy_pseudofs
	ret = ihk_os_destroy_pseudofs(0, 0, 0);
	fp = popen("cat /proc/mounts | grep /tmp/mcos/mcos0_sys", "r");
	nread = fread(buf, 1, sizeof(buf), fp);
	buf[nread] = 0;

	sprintf(cmd, "insmod %s/kmod/ihk.ko", prefix);
	status = system(cmd);
	NG(status == 0, "insmod ihk.ko");

	sprintf(cmd, "insmod %s/kmod/ihk-smp-x86_64.ko ihk_start_irq=240 ihk_ikc_irq_core=0", prefix);
	status = system(cmd);
	NG(status == 0, "insmod ihk-smp-x86_64.ko");

	sprintf(cmd, "insmod %s/kmod/mcctrl.ko", prefix);
	status = system(cmd);
	NG(status == 0, "insmod mcctrl.ko");

	// reserve cpus
	ret = ihk_reserve_cpu(0, cpus, num_cpus);
	OKNG(ret == 0, "ihk_reserve_cpu");

	// reserve mem 128m@0,64m@0
	num_mem_chunks = 2;
	mem_chunks[0].size = 128*1024*1024ULL;
	mem_chunks[0].numa_node_number = 0;
	mem_chunks[1].size = 64*1024*1024ULL;
	mem_chunks[1].numa_node_number = 0;
	ret = ihk_reserve_mem(0, mem_chunks, num_mem_chunks);
	OKNG(ret == 0, "ihk_reserve_mem (2)");

	/*--------------------------------------------
	 * Test
	 *--------------------------------------------
	 */
start:
	// create 0
	ret = ihk_create_os(0);
	OKNG(ret == 0, "ihk_create_os");

	// assign cpus
	ret = ihk_os_assign_cpu(0, cpus, num_cpus);
	OKNG(ret == 0, "ihk_os_assign_cpu");

	// assign mem 128m@0,128m@1
	ret = ihk_os_assign_mem(0, mem_chunks, num_mem_chunks);
	OKNG(ret == 0, "ihk_os_assign_mem (2)");

	// load
	sprintf(fn, "%s/smp-x86/kernel/mckernel.img", prefix);
	ret = ihk_os_load(0, fn);
	OKNG(ret == 0, "ihk_os_load");

	// kargs
	sprintf(kargs, "hidos ksyslogd=0");
	ret = ihk_os_kargs(0, kargs);
	OKNG(ret == 0, "ihk_os_kargs");

	// boot
	ret = ihk_os_boot(0);
	OKNG(ret == 0, "ihk_os_boot");

	// actual test: setperfevent right after boot
	ret = ihk_os_setperfevent(0, perfattr, 1);
	OKNG(ret > 0, "setperfevent, i: %d", i);

shutdown:
	// shutdown
	ret = ihk_os_shutdown(0);
	OKNG(ret == 0, "shutdown");

	// get status
	i = 0;
	while ((ret = ihk_os_get_status(0)) == IHK_STATUS_SHUTDOWN &&
		i++ < 1000000) {
	}
	OKNG(ret == IHK_STATUS_INACTIVE,
		"waiting for shutdown, i: %d", i);
destroy:
	ret = ihk_destroy_os(0, 0);
	OKNG(ret == 0, "destroy");


	if (loop++ < 4096)
		goto start;

	sprintf(cmd, "rmmod %s/kmod/mcctrl.ko", prefix);
	status = system(cmd);
	OKNG(status == 0, "rmmod mcctrl");

	// destroy pseudofs
	ret = ihk_os_destroy_pseudofs(0, 0, 0);
	fp = popen("cat /proc/mounts | grep /tmp/mcos/mcos0_sys", "r");
	nread = fread(buf, 1, sizeof(buf), fp);
	buf[nread] = 0;

	sprintf(cmd, "rmmod %s/kmod/ihk-smp-x86_64.ko", prefix);
	status = system(cmd);
	NG(status == 0, "rmmod ihk-smp-x86_64");

	sprintf(cmd, "rmmod %s/kmod/ihk.ko", prefix);
	status = system(cmd);
	NG(status == 0, "rmmod ihk");

	printf("*** All tests finished\n");

fn_exit:
	return ret;
fn_fail:
	goto fn_exit;
}
