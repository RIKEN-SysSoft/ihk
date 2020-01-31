/* ihklib.h */
#ifndef INCLUDED_IHKLIB
#define INCLUDED_IHKLIB

#include <stdio.h>
#include <unistd.h>

#include <bfd.h>
#define PATH_SYS_NODE "/sys/devices/system/node"
#define PATH_DEV "/dev"

#include <ihk/affinity.h> 
#include <ihk/ihk_rusage.h>

#ifndef IHK_OS_EVENTFD_TYPE_DEFINED
#define IHK_OS_EVENTFD_TYPE_DEFINED
enum ihk_os_eventfd_type {
	IHK_OS_EVENTFD_TYPE_OOM = 0, /* Raise an event when physical memory used exceeds the limit */
	IHK_OS_EVENTFD_TYPE_STATUS = 2, /* Raise an event when detecting hung-up or panic */
	IHK_OS_EVENTFD_TYPE_KMSG = 101,
	/* Raise an event when kmsg buffer is full. The kmsg taker is expected to take the kmsg. */
};
#endif

struct ihk_mem_chunk {
	unsigned long size;
	int numa_node_number;
};

struct ihk_ikc_cpu_map {
	int src_cpu; /* LWK CPU as IKC source */
	int dst_cpu; /* Linux CPU as IKC destination */
};

enum ihklib_os_status {
	IHK_STATUS_INACTIVE,
	IHK_STATUS_BOOTING,
	IHK_STATUS_RUNNING,
	IHK_STATUS_SHUTDOWN,
	IHK_STATUS_PANIC,
	IHK_STATUS_HUNGUP,
	IHK_STATUS_FREEZING,
	IHK_STATUS_FROZEN,
};

enum ihk_perf_event {
	PERF_EVENT_ENABLE,
	PERF_EVENT_DISABLE,
	PERF_EVENT_DESTROY,
};

typedef struct ihk_perf_event_attr {
	unsigned long config; 
	unsigned disabled:1;
	unsigned pinned:1;
	unsigned exclude_user:1;
	unsigned exclude_kernel:1;
	unsigned exclude_hv:1;
	unsigned exclude_idle:1;
} ihk_perf_event_attr;

enum IHKLIB_LOGLEVEL {
	IHKLIB_LOGLEVEL_EMERG = 0,
	IHKLIB_LOGLEVEL_ERR
};

enum ihk_reserve_mem_conf_keys {
	IHK_RESERVE_MEM_TOTAL,
	IHK_RESERVE_MEM_MIN_CHUNK_SIZE,
	IHK_RESERVE_MEM_MAX_SIZE_RATIO_ALL,
	IHK_RESERVE_MEM_TIMEOUT,
};

extern int loglevel;

int ihk_reserve_cpu(int index, int* cpus, int num_cpus);
int ihk_get_num_reserved_cpus(int index);
int ihk_query_cpu(int index, int* cpus, int _num_cpus);
int ihk_release_cpu(int index, int* cpus, int num_cpus);
int ihk_reserve_mem_conf(int index, int key, void *value);
int ihk_reserve_mem(int index, struct ihk_mem_chunk* mem_chunks, int num_mem_chunks);
int ihk_get_num_reserved_mem_chunks(int index);
int ihk_query_mem(int index, struct ihk_mem_chunk* mem_chunks, int _num_mem_chunks);
int ihk_release_mem(int index, struct ihk_mem_chunk* mem_chunks, int num_mem_chunks);
int ihk_create_os(int index);
int ihk_get_num_os_instances(int index);
int ihk_get_os_instances(int index, int *indices, int _num_os_instances);
int ihk_destroy_os(int dev_index, int os_index);
;
int ihk_os_assign_cpu(int index, int* cpus, int num_cpus);
int ihk_os_get_num_assigned_cpus(int index);
int ihk_os_query_cpu(int index, int* cpus, int _num_cpus);
int ihk_os_release_cpu(int index, int* cpus, int num_cpus);
int ihk_os_set_ikc_map(int index, struct ihk_ikc_cpu_map *map, int num_cpus);
int ihk_os_get_ikc_map(int index, struct ihk_ikc_cpu_map *map, int num_cpus);
int ihk_os_assign_mem(int index, struct ihk_mem_chunk *mem_chunks, int num_mem_chunks);
int ihk_os_get_num_assigned_mem_chunks(int index);
int ihk_os_query_mem(int index, struct ihk_mem_chunk* mem_chunks, int _num_mem_chunks);
int ihk_os_release_mem(int index, struct ihk_mem_chunk* mem_chunks, int num_mem_chunks);
int ihk_os_get_eventfd(int index, int type);
int ihk_os_load(int index, char* fn);
int ihk_os_kargs(int index, char* kargs);
int ihk_os_boot(int index);
int ihk_os_shutdown(int index);
int ihk_os_get_status(int index);
int ihk_os_get_kmsg_size(int index);
int ihk_os_kmsg(int index, char* kmsg, ssize_t sz_kmsg);
int ihk_os_clear_kmsg(int index);
int ihk_os_get_num_numa_nodes(int index);
int ihk_os_query_free_mem(int os_index, unsigned long *memfree, int num_numa_nodes);
int ihk_os_query_total_mem(int os_index, unsigned long *memtotal, int num_numa_nodes);
int ihk_os_get_num_pagesizes(int index);
int ihk_os_get_pagesizes(int index, long *pgsizes, int num_pgsizes);
int ihk_os_getrusage(int index, struct ihk_os_rusage *rusage, size_t size_rusage);
int ihk_os_setperfevent(int index, ihk_perf_event_attr *attr, int n);
int ihk_os_perfctl(int index, int comm);
int ihk_os_getperfevent(int index, unsigned long *counter, int n);
int ihk_os_freeze(unsigned long *os_set, int n);
int ihk_os_thaw(unsigned long *os_set, int n);
int ihk_os_makedumpfile(int index, char *dump_file, int dump_level, int interactive);
int ihk_set_loglevel(enum IHKLIB_LOGLEVEL level);

#endif

