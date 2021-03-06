#ifndef IHKLIB_PRIVATE_H_INCLUDED
#define IHKLIB_PRIVATE_H_INCLUDED

#ifndef __KERNEL__

#include <ihk/ihklib.h>
#include <ihk/ihk_host_user.h>
#include <ihk/ihk_rusage.h>

#define printk(fmt, args...) do {					\
	char contents[IHKLIB_LINUX_KMSG_SIZE];				\
	int _fd;							\
	ssize_t len;							\
	ssize_t offset = 0;						\
	ssize_t written;						\
									\
	if (geteuid()) {						\
		break;							\
	}								\
	snprintf(contents, IHKLIB_LINUX_KMSG_SIZE, fmt, ##args);	\
	_fd = open("/dev/kmsg", O_WRONLY);				\
	len = strnlen(contents, IHKLIB_LINUX_KMSG_SIZE - 1) + 1;	\
	while (offset < len) {						\
		written = write(_fd, contents + offset, len - offset);	\
		if (written <= 0) {					\
			break;						\
		}							\
		offset += written;					\
	}								\
	close(_fd);							\
} while (0)

#define Q(x) #x
#define QUOTE(x) Q(x)

struct ihk_ioctl_desc {
	char *string;
	int string_len;
};

struct mcctrl_ioctl_getrusage_desc {
	struct ihk_os_rusage *rusage;
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

struct ihklib_reserve_mem_conf {
	/* 1: Try to reserve the sum of the requested per-NUMA-node
	 *    amounts in a balanced way. It reports an error when the
	 *    variance of the reserved amounts of the NUMA-nodes
	 *    exceeds the "variance_limit" below.
	 */
	int balanced_enable;

	/* 1: Perform best-effort reservation of the size up to the
	 *    requested size, i.e. it's allowed to lower the reservation
	 *    size to the available if the latter is less than the former.
	 */
	int balanced_best_effort;

	/* MAX(max - ave, ave - min) must be less than or equal to
	 * ave * variance_limit / 100
	 */
	int balanced_variance_limit;

	/* Limit of alloc_pages order when reserving.
	 * Use PAGE_SIZE for a system with system memory isolated,
	 * 32 KiB (1 MiB for "all") otherwise.
	 */
	int min_chunk_size;

	/* Stop gathering chunks for "all" request after accumulating
	 * this percentage
	 */
	int max_size_ratio_all;

	/* Give up proceeding to the smaller order when it took longer
	 * than this seconds for the current order
	 */
	int timeout;
};

extern struct ihklib_reserve_mem_conf reserve_mem_conf;

int ihklib_device_open(int index);
int ihklib_os_open(int index);
int ihklib_os_query_mem_sysfs(int index, char *result, ssize_t sz_result,
			      const char *kind);

int cpu_str2count(char *cpu_list);
int cpu_str2req(char *_cpu_list, int num_cpus, struct ihk_cpu_req *req);
char *cpu_req2str(struct ihk_cpu_req *req);
int mem_str2count(char *mem_list);
int mem_str2req(char *_mem_list, struct ihk_mem_req *req);
char *mem_req2str(struct ihk_mem_req *req);
int ikc_str2count(char *ikc_list);
int ikc_str2req(char *_ikc_list, int num_cpus, struct ihk_ikc_req *req);
char *ikc_req2str(struct ihk_ikc_req *req);

int _ihk_reserve_cpu_str(int dev_index, char *list, char *err_msg);
int _ihk_reserve_mem_str(int dev_index, char *list, char *err_msg);

#endif /* !defined(__KERNEL__) */

#define IHK_MAX_NUM_MEM_CHUNKS 2048

#define IHK_OS_EVENTFD_MONITOR_INTERVAL (1000*1000*2) /* usec */

#define IHKLIB_MAX_SIZE_ENV (1UL << 20)
#define IHKLIB_MAX_NUM_ENV (1UL << 10)
#define IHKLIB_MAX_SIZE_ERR_MSG (1UL << 12)
#define IHKLIB_MAX_SIZE_KARGS (1UL << 20)
#define IHKLIB_LINUX_KMSG_SIZE (4096 - 256)

/* taking more than this percentage of memory would
 * make Linux panic due to out of memory
 */
#ifndef ENABLE_FUGAKU_HACKS
#define IHK_RESERVE_MEM_MAX_SIZE_RATIO_ALL_LIMIT 98
#else
#define IHK_RESERVE_MEM_MAX_SIZE_RATIO_ALL_LIMIT 95
#endif

#endif /* !defined(IHKLIB_PRIVATE_H_INCLUDED) */
