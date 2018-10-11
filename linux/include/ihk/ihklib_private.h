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

struct namespace_file {
	int nstype;
	const char *name;
	int fd;
};

enum ihklib_os_query_mem_type {
	IHKLIB_OS_QUERY_MEM_TOTAL,
	IHKLIB_OS_QUERY_MEM_FREE
};

const char *ihklib_os_query_mem_type_str[] = {
	[IHKLIB_OS_QUERY_MEM_TOTAL] = "MemTotal",
	[IHKLIB_OS_QUERY_MEM_FREE] = "MemFree"
};

int ihklib_device_open(int index);
int ihklib_os_open(int index);
int ihklib_os_query_mem_sysfs(int index, char *result, ssize_t sz_result,
			      const char *kind);

#endif /* !defined(IHKLIB_PRIVATE_H_INCLUDED) */
