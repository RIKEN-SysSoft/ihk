#ifndef IHKLIB_PRIVATE_H_INCLUDED
#define IHKLIB_PRIVATE_H_INCLUDED
#include <ihk/ihklib.h>
#include <ihk/ihk_rusage.h>
#include <driver/ihk_host_user.h>

#define IHK_MAX_NUM_MEM_CHUNKS 2048

#define IHK_OS_EVENTFD_MONITOR_INTERVAL (1000*1000*2) /* usec */

#define IHKLIB_TEST_MODE_ENV_NAME "IHKLIB_TEST_MODE"

typedef enum ihklib_test_mode {
  //TEST_NONE                                        = 0,
  /* ihk_reserve_cpu */
  TEST_IHK_RESERVE_CPU                             = 1,
  TEST_IHKLIB_DEVICE_OPEN                          = 2,
  TEST_IHKLIB_DEVICE_READABLE                      = 3,

  /* ihk_reserve_mem */
  TEST_IHK_RESERVE_MEM                             = 4,
  TEST_IHK_RESERVE_MEM_CONF                        = 5,

  /* ihk_query_cpu */
  TEST_IHK_QUERY_CPU                               = 6,

  /* ihk_get_num_reserved_cpus */
  TEST_IHK_GET_NUM_RESERVED_CPUS                   = 7,

  /* ihk_release_cpu */
  TEST_IHK_RELEASE_CPU                             = 8,

  /* int ihk_get_num_reserved_mem_chunks */
  TEST_IHK_GET_NUM_RESERVED_MEM_CHUNKS             = 9,

  /* ihk_query_mem  */
  TEST_IHK_QUERY_MEM                               = 10,

  /* ihk_release_mem */
  TEST_IHK_RELEASE_MEM                             = 11,

  /* ihk_get_num_os_instances */
  TEST_IHK_GET_NUM_OS_INSTANCES                    = 12,

  /* ihk_get_os_instances */
  TEST_IHK_GET_OS_INSTANCES                        = 13,

  /* ihk_create_os */
  TEST_IHK_CREATE_OS                               = 14,

  /* ihk_destroy_os */
  TEST_IHK_DESTROY_OS                              = 15,

  /* ihk_os_set_ikc_map */
  TEST_IHK_OS_SET_IKC_MAP                          = 16,

  /* ihk_os_get_ikc_map */
  TEST_IHK_OS_GET_IKC_MAP                          = 17,

  /* ihk_os_release_cpu */
  TEST_IHK_OS_RELEASE_CPU                          = 18,

  /* ihk_os_query_cpu */
  TEST_IHK_OS_QUERY_CPU                            = 19,

  /* ihk_os_get_num_assigned_cpus */
  TEST_IHK_OS_GET_NUM_ASSIGNED_CPUS                = 20,

  /* ihk_os_assign_cpu */
  TEST_IHK_OS_ASSIGN_CPU                           = 21,

  /* ihk_os_assign_mem */
  TEST_IHK_OS_ASSIGN_MEM                           = 22,

  /* ihk_os_get_num_assigned_mem_chunks */
  TEST_IHK_OS_GET_NUM_ASSIGNED_MEM_CHUNKS          = 23,

  /* ihk_os_query_mem */
  TEST_IHK_OS_QUERY_MEM                            = 24,

  /* ihk_os_release_mem */
  TEST_IHK_OS_RELEASE_MEM                          = 25,

  /* ihk_os_get_eventfd */
  TEST_IHK_OS_GET_EVENTFD                          = 26,

  /* ihk_os_shutdown */
  TEST_IHK_OS_SHUTDOWN                             = 27,

  /* ihk_os_boot */
  TEST_IHK_OS_BOOT                                 = 28,

  /* ihk_os_get_status */
  TEST_IHK_OS_GET_STATUS                           = 29,

  /* ihk_os_get_num_numa_nodes */
  TEST_IHK_OS_GET_NUM_NUMA_NODES                   = 30,

  /* ihklib_os_query_mem */
  TEST_IHKLIB_OS_QUERY_MEM                         = 31,
  TEST_IHKLIB_OS_QUERY_MEM_SYSFS                   = 32,

  /* ihk_os_get_pagesizes */
  TEST_RUSAGE_PGTYPE_TO_PGSIZE                     = 33,

  /* ihk_os_setperfevent */
  TEST_IHK_OS_SETPERFEVENT                         = 34,

  /* ihk_os_perfctl */
  TEST_IHK_OS_PERFCTL                              = 35,

  /* ihk_os_freeze */
  TEST_IHK_OS_FREEZE                               = 36,

  /* ihk_os_thaw */
  TEST_IHK_OS_THAW                                 = 37,

  /* ihk_os_makedumpfile */
  TEST_IHK_OS_MAKEDUMPFILE                         = 38,
  TEST_SMP_IHK_OS_DUMP                             = 39,
  TEST_IHK_READ_KMSG                               = 40

} ihklib_test_mode_t;

struct ihk_ioctl_desc {
  char *string;
  int string_len;
};

struct ihk_ioctl_cpu_desc {
  int *cpus;
  int num_cpus;
};

struct ihk_ioctl_ikc_desc {
  int *src_cpus;  /* LWC CPUs as IKC source */
  int *dst_cpus;  /* Linux CPUs as IKC destination */
  int num_cpus;
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
  int total;

  /* MAX(max - ave, ave - min) must be less than or equal to
   * ave * variance_limit / 100
   */
  int variance_limit;

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
int mem_str2req(char *_mem_list, int num_mem_chunks, struct ihk_mem_req *req);
char *mem_req2str(struct ihk_mem_req *req);
int ikc_str2count(char *ikc_list);
int ikc_str2req(char *_ikc_list, int num_cpus, struct ihk_ikc_req *req);
char *ikc_req2str(struct ihk_ikc_req *req);

#endif /* !defined(IHKLIB_PRIVATE_H_INCLUDED) */
