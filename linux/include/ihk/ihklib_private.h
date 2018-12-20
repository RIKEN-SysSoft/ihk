#ifndef IHKLIB_PRIVATE_H_INCLUDED
#define IHKLIB_PRIVATE_H_INCLUDED

//#include <ihk/ihk_monitor.h>
#include <ihk/ihklib.h>
#include <ihk/ihk_host_user.h>

#define IHK_MAX_NUM_PGSIZES 4
#define IHK_MAX_NUM_NUMA_NODES 32
#define IHK_MAX_NUM_CPUS 1024
#define IHK_MAX_STR_LEN 65536

#define IHK_MAX_NUM_MEM_CHUNKS 2048

#define IHK_OS_EVENTFD_MONITOR_INTERVAL (1000*1000*2) /* usec */

struct ihk_ioctl_desc {
	char *string;
	int string_len;
};

struct ihk_ioctl_cpu_desc {
	int *cpus;
	int num_cpus;
};

struct ihk_ioctl_mem_desc {
	size_t *sizes;
	int *numa_ids;
	int num_chunks;
};

struct ihk_ioctl_ikc_desc {
	int *src_cpus;	/* LWC CPUs as IKC source */
	int *dst_cpus;	/* Linux CPUs as IKC destination */
	int num_cpus;
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

int cpu_str2count(char *cpu_list);
int cpu_str2req(char *_cpu_list, int num_cpus, ihk_cpu_req_t *req);
void cpu_req2str(char *str, ssize_t len, ihk_cpu_req_t *req);
int mem_str2count(char *mem_list);
int mem_str2req(char *_mem_list, int num_mem_chunks, ihk_mem_req_t *req);
void mem_req2str(char *str, ssize_t len, ihk_mem_req_t *req);
int ikc_str2count(char *ikc_list);
int ikc_str2req(char *_ikc_list, int num_cpus, ihk_ikc_req_t *req);
void ikc_req2str(char *str, ssize_t len, ihk_ikc_req_t *req);

#endif /* !defined(IHKLIB_PRIVATE_H_INCLUDED) */
