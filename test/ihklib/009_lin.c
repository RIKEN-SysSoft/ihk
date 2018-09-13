#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ihklib.h>
#include <mckernel/ihklib_rusage.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <errno.h>
#include "util.h"

#define DEBUG

int main(int argc, char** argv) {
	int ret, status;
	int i, j;
	FILE *fp;
	char buf[65536];
	size_t nread;

	char cmd[1024];
	char fn[256];
	char kargs[256];

	int cpus[4];
	int num_cpus;

	struct ihk_mem_chunk mem_chunks[4];
	int num_mem_chunks;

	int evfd;
	int epfd;
	struct epoll_event event;
	struct epoll_event events[1];

	if(geteuid() != 0) {
		printf("Execute as a root\n");
	}	

	// kill ihkmond
	status = system("pid=`pidof ihkmond`&&if [ \"${pid}\" != \"\" ]; then kill -9 ${pid}; fi");

	// run ihkmond
	sprintf(cmd, "%s/sbin/ihkmond -f LOG_LOCAL5 -k 1 -i 2", QUOTE(MCK_DIR));
	status = system(cmd);
	CHKANDJUMP(WEXITSTATUS(status) != 0, -1, "system /sbin/ihkmond");

	// ihk_os_destroy_pseudofs
	ret = ihk_os_destroy_pseudofs(0, 0, 0);
	fp = popen("cat /proc/mounts | grep /tmp/mcos/mcos0_sys", "r");
	nread = fread(buf, 1, sizeof(buf), fp);
	buf[nread] = 0;
	OKNG(ret == 0 &&
	     strstr(buf, "/tmp/mcos/mcos0_sys") == NULL,
	     "ihk_os_destroy_pseudofs (1)\n");

	sprintf(cmd, "insmod %s/kmod/ihk.ko", QUOTE(MCK_DIR));
	status = system(cmd);
	CHKANDJUMP(WEXITSTATUS(status) != 0, -1, "system insmod");

	sprintf(cmd, "insmod %s/kmod/ihk-smp-%s.ko ihk_start_irq=240 ihk_ikc_irq_core=0", QUOTE(MCK_DIR), QUOTE(ARCH));
	status = system(cmd);
	CHKANDJUMP(WEXITSTATUS(status) != 0, -1, "system insmod");

	sprintf(cmd, "chown takagi:takagi /dev/mcd*\n");
	status = system(cmd);
	CHKANDJUMP(WEXITSTATUS(status) != 0, -1, "system chown");

	sprintf(cmd, "insmod %s/kmod/mcctrl.ko", QUOTE(MCK_DIR));
	status = system(cmd);
	CHKANDJUMP(WEXITSTATUS(status) != 0, -1, "system insmod");

	// reserve cpu
	cpus[0] = 3;
	cpus[1] = 1;
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

	for(j = 0; j < 3; j++) {
		// create 0
		ret = ihk_create_os(0);
		OKNG(ret == 0, "ihk_create_os (2)\n");

		sprintf(cmd, "chown takagi:takagi /dev/mcos*\n");
		status = system(cmd);
		CHKANDJUMP(WEXITSTATUS(status) != 0, -1, "system chown");

		// assign cpu 3,1
		num_cpus = 2;
		cpus[0] = 3;
		cpus[1] = 1;
		ret = ihk_os_assign_cpu(0, cpus, num_cpus);
		OKNG(ret == 0, "ihk_os_assign_cpu\n");

		// assign mem 128m@0,64m@0
		num_mem_chunks = 2;
		mem_chunks[0].size = 128*1024*1024ULL;
		mem_chunks[0].numa_node_number = 0;
		mem_chunks[1].size = 64*1024*1024ULL;
		mem_chunks[1].numa_node_number = 0;
		ret = ihk_os_assign_mem(0, mem_chunks, num_mem_chunks);
		OKNG(ret == 0, "ihk_os_assign_mem (2)\n");

		// load
		sprintf(fn, "%s/%s/kernel/mckernel.img", QUOTE(MCK_DIR), QUOTE(TARGET));
		ret = ihk_os_load(0, fn);
		OKNG(ret == 0, "ihk_os_load\n");

		// kargs
		sprintf(kargs, "hidos ksyslogd=1");
		ret = ihk_os_kargs(0, kargs);
		OKNG(ret == 0, "ihk_os_kargs\n");

		// boot
		ret = ihk_os_boot(0);
		OKNG(ret == 0, "ihk_os_boot\n");

		usleep(100*1000);

		if(j == 0) {
			// create pseudofs
			ret = ihk_os_create_pseudofs(0, 0, 0);
			fp = popen("cat /proc/mounts | grep /tmp/mcos/mcos0_sys", "r");
			nread = fread(buf, 1, sizeof(buf), fp);
			buf[nread] = 0;
			OKNG(ret == 0 &&
				 strstr(buf, "/tmp/mcos/mcos0_sys") != NULL, "ihk_os_create_pseudofs()\n");
		}

		// Check kmsg size
		ret = ihk_os_get_kmsg_size(0);
		OKNG(ret == 8192, "ihk_os_get_kmsg_size\n");

		// mcexec
		sprintf(cmd, "%s/bin/mcexec ./ihklib009_mck&", QUOTE(MCK_DIR));
		status = system(cmd);

		// epoll
		evfd = ihk_os_get_eventfd(0, IHK_OS_EVENTFD_TYPE_STATUS);
		CHKANDJUMP(evfd < 0, 255, "geteventfd failed\n");
		
		epfd = epoll_create(1);
		CHKANDJUMP(epfd == -1, 255, "epoll_create failed\n");
		
		memset(&event, 0, sizeof(struct epoll_event));
		event.events = EPOLLIN;
		event.data.fd = evfd;
		ret = epoll_ctl(epfd, EPOLL_CTL_ADD, evfd, &event);
		CHKANDJUMP(ret != 0, 255, "epoll_ctl failed\n");
		
		int nfd = epoll_wait(epfd, events, 1, 1000 * 10);
		if(nfd == 0) {
			OKNG(0, "ihk_os_get_eventfd STATUS (%d)\n", j + 1);
		} else {
			for (i = 0; i < nfd; i++) {
				if (events[i].data.fd == evfd) {
					uint64_t counter;
					ssize_t nread = read(evfd, &counter, sizeof(counter));
					CHKANDJUMP(nread == 0, 255, "EOF detected\n");
					CHKANDJUMP(nread == -1, 255, "error: %s\n", strerror(errno));
					OKNG(1, "ihk_os_get_eventfd STATUS (%d)\n", j + 1);
				}
			}
		}

		// kill mcexec
		status = system("pid=`pidof mcexec`&&if [ \"${pid}\" != \"\" ]; then kill -9 ${pid}; fi");
		CHKANDJUMP(WEXITSTATUS(status) != 0, -1, "system");

		// destroy os
		for(i = 0; i < 4; i++) {
			usleep(250 * 1000); // Wait for nothing is in-flight
			ret = ihk_destroy_os(0, 0);
			if (ret == 0) {
				OKNG(1, "ihk_destroy_os (4), trial #%d succeeded\n", i + 1);
				goto success;
			}
		}
		OKNG(0, "ihk_destroy_os failed\n");
	success:;
	}

	// kill ihkmond
	status = system("pid=`pidof ihkmond`&&if [ \"${pid}\" != \"\" ]; then kill -9 ${pid}; fi");
	CHKANDJUMP(WEXITSTATUS(status) != 0, -1, "system");

	// rmmod mcctrl
	sprintf(cmd, "rmmod %s/kmod/mcctrl.ko", QUOTE(MCK_DIR));
	status = system(cmd);
	CHKANDJUMP(WEXITSTATUS(status) != 0, -1, "system rmmod");
#if 1
	// destroy pseudofs
	ret = ihk_os_destroy_pseudofs(0, 0, 0);
	fp = popen("cat /proc/mounts | grep /tmp/mcos/mcos0_sys", "r");
	nread = fread(buf, 1, sizeof(buf), fp);
	buf[nread] = 0;
	OKNG(ret == 0 &&
		 strstr(buf, "/tmp/mcos/mcos0_sys") == NULL, "ihk_os_destroy_pseudofs (3)\n");
#endif
	// rmmod ihk-smp-<QUOTE(ARCH)>
	sprintf(cmd, "rmmod %s/kmod/ihk-smp-%s.ko", QUOTE(MCK_DIR), QUOTE(ARCH));
	status = system(cmd);
	CHKANDJUMP(WEXITSTATUS(status) != 0, -1, "system rmmod");

	// rmmod ihk
	sprintf(cmd, "rmmod %s/kmod/ihk.ko", QUOTE(MCK_DIR));
	status = system(cmd);
	CHKANDJUMP(WEXITSTATUS(status) != 0, -1, "system rmmod");

	printf("[INFO] All tests finished\n");
	ret = 0;

 fn_exit:
	return ret;
 fn_fail:
	// destroy os
	for(i = 0; i < 4; i++) {
		usleep(250 * 1000); // Wait for nothing is in-flight
		ret = ihk_destroy_os(0, 0);
		if (ret == 0) {
			break;
		}
	}
	if(i == 4) {
		printf("ihk_destroy_os failed four times\n");
	}

	// kill ihkmond
	status = system("pid=`pidof ihkmond`&&if [ \"${pid}\" != \"\" ]; then kill -9 ${pid}; fi");
    goto fn_exit;
}
