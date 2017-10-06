#ifndef IHKLIB_PRIVATE_H_INCLUDED
#define IHKLIB_PRIVATE_H_INCLUDED

//#include <ihk/ihk_monitor.h>

#define IHK_MAX_NUM_PGSIZES 4
#define IHK_MAX_NUM_NUMA_NODES 32
#define IHK_MAX_NUM_CPUS 1024

#define IHK_MAX_NUM_MEM_CHUNKS 2048

#define IHK_OS_EVENTFD_MONITOR_INTERVAL (1000*1000*2) /* usec */

struct ihk_ioctl_desc {
	char *string;
	int string_len;
};

struct mcctrl_ioctl_getrusage_desc {
	void* rusage;
	size_t size_rusage;
};

int ihklib_device_open(int index);
int ihklib_os_open(int index);

#endif /* !defined(IHKLIB_PRIVATE_H_INCLUDED) */
