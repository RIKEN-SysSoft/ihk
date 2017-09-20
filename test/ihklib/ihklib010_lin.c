#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ihklib.h>
#include <mckernel/ihklib_rusage.h>
#include <sys/types.h>

#define DEBUG

#ifdef DEBUG
#define dprintf(...)                                            \
    do {                                                        \
	char msg[1024];                                         \
	sprintf(msg, __VA_ARGS__);                              \
	fprintf(stdout, "%s,%s", __FUNCTION__, msg);            \
    } while (0);
#define eprintf(...)                                            \
    do {                                                        \
	char msg[1024];                                         \
	sprintf(msg, __VA_ARGS__);                              \
	fprintf(stdout, "%s,%s", __FUNCTION__, msg);            \
    } while (0);
#else
#define dprintf(...) do {  } while (0)
#define eprintf(...) do {  } while (0)
#endif

#define CHKANDJUMP(cond, err, ...)                                      \
    do {                                                                \
		if(cond) {                                                      \
			eprintf(__VA_ARGS__);                                       \
			ret = err;                                                  \
			goto fn_fail;                                               \
		}                                                               \
    } while(0)


#define OKNG(cond, ...)													\
    do {                                                                \
		if(cond) {                                                      \
			printf("[OK] ");											\
			printf(__VA_ARGS__);										\
		} else {														\
            printf("[NG] ");											\
			printf(__VA_ARGS__);										\
			goto fn_fail;												\
		}																\
    } while(0)

#define PREFIX "/home/takagi/project/os/install"


int main(int argc, char** argv) {
    int ret = 0, status, ret_ihklib, ret_internal;
	int i;
	FILE *fp, *fp1, *fp2;
	char buf[65536], buf1[65536], buf2[65536];
	size_t nread;

	char cmd[1024];
	char token[256];
	char fn[256];
	char kargs[256];

	int cpus[4];
	int num_cpus;

	struct ihk_mem_chunk mem_chunks[4];
	int num_mem_chunks;

	int indices[2];
	int num_os_instances;

	ssize_t kmsg_size;

	struct ihk_ikc_cpu_map ikc_map[2];

	int num_numa_nodes;
	long memfree[4];
	
	int num_pgsizes;
	long pgsizes[3];
	
	struct mckernel_rusage rusage;

	if(geteuid() != 0) {
		printf("Execute as a root like: sudo bash -c 'LD_LIBRARY_PATH=/home/takagi/project/os/install/lib/ %s'", argv[0]);
	}	

	// kill ihkmond
	status = system("pid=`pidof ihkmond`&&if [ \"${pid}\" != \"\" ]; then kill -9 ${pid}; fi");

	// run ihkmond
	status = system(PREFIX "/sbin/ihkmond -f LOG_LOCAL5 -k 1 -i 3600");
	CHKANDJUMP(WEXITSTATUS(status) != 0, -1, "system /sbin/ihkmond");

	// ihk_os_destroy_pseudofs
	ret_ihklib = ihk_os_destroy_pseudofs(0);
	fp = popen("cat /proc/mounts | grep /tmp/mcos/mcos0_sys", "r");
	nread = fread(buf, 1, sizeof(buf), fp);
	buf[nread] = 0;
	OKNG(ret_ihklib == 0 &&
		 strstr(buf, "/tmp/mcos/mcos0_sys") == NULL, "ihk_os_destroy_pseudofs (1)\n");

	/*--------------------------------------------*/
	/* Expected to succeed
	/*--------------------------------------------*/

	sprintf(cmd, "insmod %s/kmod/ihk.ko", PREFIX);
	status = system(cmd);
	CHKANDJUMP(WEXITSTATUS(status) != 0, -1, "system insmod");

	sprintf(cmd, "insmod %s/kmod/ihk-smp-x86.ko ihk_start_irq=240 ihk_ikc_irq_core=0", PREFIX);
	status = system(cmd);
	CHKANDJUMP(WEXITSTATUS(status) != 0, -1, "system insmod");

	sprintf(cmd, "chown takagi:takagi /dev/mcd*\n");
	status = system(cmd);
	CHKANDJUMP(WEXITSTATUS(status) != 0, -1, "system chown");

	sprintf(cmd, "insmod %s/kmod/mcctrl.ko", PREFIX);
	status = system(cmd);
	CHKANDJUMP(WEXITSTATUS(status) != 0, -1, "system insmod");

	// reserve cpu
	cpus[0] = 3;
	cpus[1] = 1;
	num_cpus = 2;
    ret_ihklib = ihk_reserve_cpu(0, cpus, num_cpus);
    OKNG(ret_ihklib == 0, "ihk_reserve_cpu\n");

	// reserve mem 128m@0,64m@0
	num_mem_chunks = 2;
	mem_chunks[0].size = 128*1024*1024ULL;
	mem_chunks[0].numa_node_number = 0;
	mem_chunks[1].size = 64*1024*1024ULL;
	mem_chunks[1].numa_node_number = 0;
    ret_ihklib = ihk_reserve_mem(0, mem_chunks, num_mem_chunks);
    OKNG(ret_ihklib == 0, "ihk_reserve_mem\n");

	/*--------------------------------------------*/
	/* Expected to succeed                        */
	/*--------------------------------------------*/

	// create 0
    ret_ihklib = ihk_create_os(0);
	OKNG(ret_ihklib == 0, "ihk_create_os (2)\n");

	sprintf(cmd, "chown takagi:takagi /dev/mcos*\n");
	status = system(cmd);
	CHKANDJUMP(WEXITSTATUS(status) != 0, -1, "system chown");

	// assign cpu 3,1
	num_cpus = 2;
	cpus[0] = 3;
	cpus[1] = 1;
    ret_ihklib = ihk_os_assign_cpu(0, cpus, num_cpus);
    OKNG(ret_ihklib == 0, "ihk_os_assign_cpu\n");

	// assign mem 128m@0,64m@0
	num_mem_chunks = 2;
	mem_chunks[0].size = 128*1024*1024ULL;
	mem_chunks[0].numa_node_number = 0;
	mem_chunks[1].size = 64*1024*1024ULL;
	mem_chunks[1].numa_node_number = 0;
    ret_ihklib = ihk_os_assign_mem(0, mem_chunks, num_mem_chunks);
    OKNG(ret_ihklib == 0, "ihk_os_assign_mem (2)\n");

	// load
	sprintf(fn, "%s/smp-x86/kernel/mckernel.img", PREFIX);
    ret_ihklib = ihk_os_load(0, fn);
	OKNG(ret_ihklib == 0, "ihk_os_load\n");

	// kargs
	sprintf(kargs, "hidos ksyslogd=0");
    ret_ihklib = ihk_os_kargs(0, kargs);
	OKNG(ret_ihklib == 0, "ihk_os_kargs\n");

	// boot
    ret_ihklib = ihk_os_boot(0);
	OKNG(ret_ihklib == 0, "ihk_os_boot\n");

	usleep(100*1000);

#if 1
	// create pseudofs
	ret_ihklib = ihk_os_create_pseudofs(0);
	fp = popen("cat /proc/mounts | grep /tmp/mcos/mcos0_sys", "r");
	nread = fread(buf, 1, sizeof(buf), fp);
	buf[nread] = 0;
	OKNG(ret_ihklib == 0 &&
		 strstr(buf, "/tmp/mcos/mcos0_sys") != NULL, "ihk_os_create_pseudofs()\n");
#else
	_exit(255);
    // kmsg
    fp = popen("sudo bash -x " PREFIX "/sbin/mcoverlay-create.sh", "r");
    while ((nread = fread(buf, 1, sizeof(buf), fp)), nread > 0) {
		buf[nread] = 0;
        printf("%s", buf);
    }
#endif

	// mcexec
	fp = popen(PREFIX "/bin/mcexec ./ihklib010_mck", "r");
	nread = fread(buf, 1, sizeof(buf), fp);
	buf[nread] = 0;
	OKNG(strstr(buf, "ihklib010_mck exit OK") != NULL, "mcexec\n");
	
#if 0
	// kmsg
	fp = popen(PREFIX "/sbin/ihkosctl 0 kmsg", "r");
	while ((nread = fread(buf, 1, sizeof(buf), fp)), nread > 0) {
		buf[nread] = 0;
		printf("%s", buf);
	}
	_exit(255);
#endif
#if 0
	// head /var/log/local5
	printf("head -n30 /var/log/local5\n");
	fp = popen("head -n30 /var/log/local5 | tail -n10", "r");
	while ((nread = fread(buf, 1, sizeof(buf), fp)), nread > 0) {
		buf[nread] = 0;
		printf("%s", buf);
	}
#endif
	// destroy os
	for(i = 0; i < 4; i++) {
		usleep(2000 * 1000); // Wait until ihkmond sends kmsg to syslogd
		ret_ihklib = ihk_destroy_os(0, 0);
		if (ret_ihklib == 0) {
			OKNG(1, "ihk_destroy_os (4), trial #%d succeeded\n", i + 1);
			break;
		}
	}
	CHKANDJUMP(i == 4, 255, "ihk_destroy_os failed four times\n");
#define IHK_TMP "/home/takagi/project/os/install/tmp/mcos0"
#if 1
	// Check the first part of kmsg is transferred to /var/log/local5
	usleep(250 * 1000); // Wait for nothing is in-flight
	fp = popen("cat " IHK_TMP "/kmsg0", "r");
	nread = fread(buf, 1, sizeof(buf), fp);
	buf[nread] = 0;
	OKNG(strstr(buf, "first line") != NULL, "first line is transferred?\n");
#endif
#if 1
	// Check the last part of kmsg is transferred to /var/log/local5
	fp = popen("cat " IHK_TMP "/kmsg0", "r");
	nread = fread(buf, 1, sizeof(buf), fp);
	buf[nread] = 0;
	OKNG(strstr(buf, "last line") != NULL, "last line is transferred?\n");
#endif
#if 1
	// Check the size of kmsg
	fp = popen("ls -l " IHK_TMP "/kmsg0 | cut -d\" \" -f 5", "r");
	nread = fread(buf, 1, sizeof(buf), fp);
	buf[nread] = 0;
	OKNG(atol(buf) > 16384, "size of kmsg (%d) is greter than %d?\n", atol(buf), 16384);
#endif
#if 1
	// size of local5
	fp = popen("ls -l /var/log/local5 | cut -d\" \" -f 5", "r");
	nread = fread(buf, 1, sizeof(buf), fp);
	buf[nread] = 0;
	OKNG(atol(buf) > 16384, "size of local5 (%d) is greter than %d?\n", atol(buf), 16384);
#endif
#if 1
	// diff local5 kmsg
	status = system("cat /var/log/local5 | perl -e \'while(<>) {if (/: (.*)$/) {print $1.\"\\n\";}}\' > ./local5");
    CHKANDJUMP(WEXITSTATUS(status) != 0, -1, "system failed\n");
	status = system("cat " IHK_TMP "/kmsg0 | perl -e \'while(<>) {s/\\s*\\n$/\\n/; {print;}}\' > ./kmsg");
    CHKANDJUMP(WEXITSTATUS(status) != 0, -1, "system failed\n");
	fp = popen("diff ./local5 ./kmsg", "r");
	nread = fread(buf, 1, sizeof(buf), fp);
	buf[nread] = 0;
	OKNG(nread == 0, "is /tmp/kmsg the same as local5?\n");
#endif
#if 0
	// tail /var/log/local5
	printf("tail -n5 /var/log/local5\n");
	fp = popen("tail -n5 /var/log/local5", "r");
	while ((nread = fread(buf, 1, sizeof(buf), fp)), nread > 0) {
		buf[nread] = 0;
		printf("%s", buf);
	}
#endif
	// kill ihkmond
	status = system("pid=`pidof ihkmond`&&if [ \"${pid}\" != \"\" ]; then kill -9 ${pid}; fi");
	CHKANDJUMP(WEXITSTATUS(status) != 0, -1, "system");

	// rmmod mcctrl
	sprintf(cmd, "rmmod %s/kmod/mcctrl.ko", PREFIX);
	status = system(cmd);
	CHKANDJUMP(WEXITSTATUS(status) != 0, -1, "system rmmod");

	// destroy pseudofs
	ret_ihklib = ihk_os_destroy_pseudofs(0);
	fp = popen("cat /proc/mounts | grep /tmp/mcos/mcos0_sys", "r");
	nread = fread(buf, 1, sizeof(buf), fp);
	buf[nread] = 0;
	CHKANDJUMP(ret_ihklib != 0 ||
			   strstr(buf, "/tmp/mcos/mcos0_sys") != NULL, -1, "ihk_os_destroy_pseudofs (3)\n");

	// rmmod ihk-smp-x86
	sprintf(cmd, "rmmod %s/kmod/ihk-smp-x86.ko", PREFIX);
	status = system(cmd);
	CHKANDJUMP(WEXITSTATUS(status) != 0, -1, "system rmmod");

	// rmmod ihk
	sprintf(cmd, "rmmod %s/kmod/ihk.ko", PREFIX);
	status = system(cmd);
	CHKANDJUMP(WEXITSTATUS(status) != 0, -1, "system rmmod");

	printf("All tests finished\n");

 fn_exit:
    return ret;
 fn_fail:
	// destroy os
	for(i = 0; i < 4; i++) {
		usleep(250 * 1000); // Wait for nothing is in-flight
		ret_ihklib = ihk_destroy_os(0, 0);
		if (ret_ihklib == 0) {
			break;
		}
	}
	if(i == 4) {
		printf("ihk_destroy_os failed four times\n");
	}

	// kill ihkmond
	status = system("pid=`pidof ihkmond`&&if [ \"${pid}\" != \"\" ]; then kill -9 ${pid}; fi");
	// kill mcexec
	status = system("pid=`pidof mcexec`&&if [ \"${pid}\" != \"\" ]; then kill -9 ${pid}; fi");
	// mcreboot
	status = system(PREFIX "/sbin/mcstop+release.sh");

    goto fn_exit;
}
