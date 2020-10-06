/**
 * \file ihk_host_user.h
 * \brief
 *   IHK-Host: ioctl request numbers
 *   Definitions related to IHK services for administrator, implemented as user library
 * \author Taku Shimosawa  <shimosawa@is.s.u-tokyo.ac.jp> \par
 * \author Balazs Gerofi  <bgerofi@riken.jp> \par
 * Copyright (C) 2011-2017 RIKEN AICS>
 */
#ifndef __HEADER_IHK_HOST_USER_H
#define __HEADER_IHK_HOST_USER_H

#include <ihk/status.h>
#include <ihk/ihk_monitor.h>
#include <ihk/ihk_debug.h>

#define IHK_DEVICE_CREATE_OS          0x112900
#define IHK_DEVICE_DESTROY_OS         0x112901
#define IHK_DEVICE_RESERVE_CPU        0x112902
#define IHK_DEVICE_RELEASE_CPU        0x112903
#define IHK_DEVICE_RESERVE_MEM        0x112904
#define IHK_DEVICE_RELEASE_MEM        0x112905
#define IHK_DEVICE_QUERY_CPU          0x112906
#define IHK_DEVICE_QUERY_MEM          0x112907
#define IHK_DEVICE_GET_KMSG_BUF       0x112908
#define IHK_DEVICE_READ_KMSG_BUF      0x112909
#define IHK_DEVICE_RELEASE_KMSG_BUF   0x11290a
#define IHK_DEVICE_GET_BUILDID        0x11290b
#define IHK_DEVICE_GET_NUM_CPUS       0x11290c
#define IHK_DEVICE_RELEASE_MEM_PARTIALLY        0x11290d
#define IHK_DEVICE_SET_TEST_MODE      0x11290e

typedef enum ihk_test_mode {
  TEST_NONE                                       = 0,
  /* ihk_reserve_cpu */
  TEST_SMP_IHK_RESERVE_CPU                        = 1,
  TEST_IHK_DEVICE_RESERVE_CPU                     = 2,
  TEST_SMP_IHK_WRITE_CPU_SYS_FILE                 = 3,
  TEST_CPU_ARRAY2STR                              = 4,
  TEST_TRUNCATE_SNPRINTF                          = 5,

  /* ihk_reserve_mem */
  TEST_IHK_DEVICE_RESERVE_MEM                     = 6,
  TEST_IHK_SMP_RELEASE_MEM_PARTIALLY              = 7,
  TEST_SMP_IHK_RELEASE_MEM_PARTIALLY              = 8,
  TEST_IHK_DEVICE_RELEASE_MEM_PARTIALLY           = 9,
  TEST_SMP_IHK_FREE_MEM_FROM_RBTREE               = 10,
  TEST_IHK_SMP_RESERVE_MEM                        = 11,

  /* ihk_query_cpu */
  TEST_IHK_DEVICE_QUERY_CPU                       = 12,
  TEST_SMP_IHK_QUERY_CPU                          = 13,

  /* ihk_get_num_reserved_cpus */
  TEST_IHK_DEVICE_GET_NUM_CPUS                    = 14,
  TEST_SMP_IHK_GET_NUM_CPUS                       = 15,

  /* ihk_release_cpu */
  TEST_IHK_DEVICE_RELEASE_CPU                     = 16,
  TEST_SMP_IHK_RELEASE_CPU                        = 17,

  /* ihk_query_mem */
  TEST_IHK_DEVICE_QUERY_MEM                       = 18,
  TEST_SMP_IHK_QUERY_MEM                          = 19,

  /* ihk_release_mem */
  TEST_IHK_DEVICE_RELEASE_MEM                     = 20,
  TEST_SMP_IHK_RELEASE_MEM                        = 21,
  TEST_IHK_SMP_RELEASE_MEM                        = 22,
  TEST_IHK_SMP_RELEASE_CHUNK                      = 23,

  /* ihk_create_os */
  TEST_DELETE_KMSG_BUF                            = 24,
  TEST_SMP_IHK_CREATE_OS                          = 25,
  TEST_IHK_DEVICE_CREATE_OS_INIT                  = 26,
  TEST_IHK_DEVICE_CREATE_OS                       = 27,

  /* ihk_destroy_os */
  TEST_IHK_DEVICE_DESTROY_OS                      = 28,
  TEST_IHK_HOST_DEVICE_IOCTL                      = 29,
  TEST__IHK_OS_SHUTDOWN                           = 30,
  TEST_SMP_IHK_OS_WAIT_FOR_STATUS                 = 31,
  TEST_MCCTRL_OS_SHUTDOWN_NOTIFIER                = 32,
  TEST__IHK_OS_WAIT_FOR_STATUS                    = 33,
  TEST_SMP_IHK_OS_QUERY_STATUS                    = 34,
  TEST__IHK_OS_SEND_NMI                           = 35,
  TEST_SMP_IHK_OS_SEND_NMI                        = 36,
  TEST_IHK_OS_GET_SPECIAL_ADDR                    = 37,
  TEST_SMP_IHK_MAP_VIRTUAL                        = 38,
  TEST_IHK_SMP_MAP_VIRTUAL                        = 39,
  TEST_PAGER_CLEANUP                              = 40,
  TEST_SYSFSM_CLEANUP                             = 41,
  TEST__IHK_DEVICE_UNMAP_MEMORY                   = 42,
  TEST_REMOVE                                     = 43,
  TEST_FREE_TOPOLOGY_INFO                         = 44,
  TEST_FREE_NODE_TOPOLOGY                         = 45,
  TEST_FREE_CPU_TOPOLOGY_ONE                      = 46,
  TEST_FREE_CPU_TOPOLOGY                          = 47,
  TEST_DESTROY_IKC_CHANNELS                       = 48,
  TEST_IHK_IKC_DESTROY_CHANNEL                    = 49,
  TEST_MCCTRL_WAKEUP_DESC_CLEANUP                 = 50,
  TEST_PROCFS_EXIT                                = 51,
  TEST_FIND_PROCFS_ENTRY                          = 52,
  TEST_DELETE_PROCFS_ENTRIES                      = 53,
  TEST_IKC_MASTER_FINALIZE                        = 54,
  TEST_IHK_IKC_FREE_CHANNEL                       = 55,
  TEST__IHK_DEVICE_UNMAP_VIRTUAL                  = 56,
  TEST_RELEASE_KMSG_BUF                           = 57,
  TEST_ADD_FREE_MEM_CHUNK                         = 58,
  TEST_IHK_SMP_CPU_KILL                           = 59,
  TEST_IHK_SMP_GET_CPU_AFFINITY                   = 60,
  TEST_SMP_IHK_OS_UNMAP_LWK                       = 61,
  TEST_IHK_SMP_RESET_CPU                          = 62,
  TEST_SMP_IHK_OS_SHUTDOWN                        = 63,
  TEST_IHK_SMP_SET_NMI_NODE                       = 64,

  /* ihk_os_assign_cpu */
  TEST_SMP_IHK_OS_ASSIGN_CPU                      = 65,
  TEST__IHK_OS_ASSIGN_CPU                         = 66,

  /* ihk_os_query_cpu */
  TEST_SMP_IHK_OS_QUERY_CPU                       = 67,
  TEST__IHK_OS_QUERY_CPU                          = 68,

  /* ihk_os_release_cpu */
  TEST__IHK_OS_RELEASE_CPU                        = 69,
  TEST_SMP_IHK_OS_RELEASE_CPU                     = 70,

  /* ihk_os_set_ikc_map */
  TEST__IHK_OS_SET_IKC_MAP                        = 71,
  TEST_SMP_IHK_OS_SET_IKC_MAP                     = 72,

  /* ihk_os_get_ikc_map */
  TEST__IHK_OS_GET_IKC_MAP                        = 73,
  TEST_SMP_IHK_OS_GET_IKC_MAP                     = 74,

  /* ihk_os_get_num_assigned_cpus */
  TEST__IHK_OS_GET_NUM_CPUS                       = 75,

  /* ihk_os_assign_mem */
  TEST__IHK_OS_ASSIGN_MEM                         = 76,
  TEST_MERGE_MEM_CHUNKS                           = 77,
  TEST_SMP_IHK_OS_ASSIGN_MEM                      = 78,
  TEST__SMP_IHK_OS_ASSIGN_MEM                     = 79,

  /* ihk_os_get_num_assigned_mem_chunks */
  TEST__IHK_OS_QUERY_MEM                          = 80,

  /* ihk_os_query_mem */
  TEST_SMP_IHK_OS_QUERY_MEM                       = 81,

  /* ihk_os_release_mem */
  TEST__IHK_OS_RELEASE_MEM                        = 82,
  TEST__SMP_IHK_OS_RELEASE_MEM                    = 83,
  TEST_SMP_IHK_OS_RELEASE_MEM                     = 84,

  /* ihk_os_get_eventfd */
  TEST__IHK_OS_REGISTER_EVENT                     = 85,

  /* ihk_os_load */
  TEST_SMP_IHK_ARCH_VMAP_AREA_TAKEN               = 86,
  TEST_SMP_IHK_OS_LOAD_MEM                        = 87,
  TEST__IHK_OS_LOAD_FILE                          = 88,
  TEST_SMP_IHK_OS_LOAD_FILE                       = 89,
  TEST__IHK_OS_LOAD_MEMORY                        = 90,

  /* ihk_os_kargs */
  TEST__IHK_OS_SET_KARGS                          = 91,

  /* ihk_os_boot */
  TEST__IHK_OS_BOOT                               = 92,
  TEST_LINUX_NUMA_2_LWK_NUMA                      = 93,
  TEST_LWK_CPU_2_LINUX_CPU                        = 94,
  TEST_SMP_WAKEUP_SECONDARY_CPU                   = 95,
  TEST_IHK_IKC_MASTER_INIT                        = 96,
  TEST_IHK_HOST_IKC_INIT_FIRST                    = 97,
  TEST__IHK_OS_REGISTER_HANDLER                   = 98,
  TEST__IHK_OS_GET_SPECIAL_ADDR                   = 99,
  TEST_IHK_DEVICE_MAP_MEMORY                      = 100,
  TEST__IHK_DEVICE_MAP_MEMORY                     = 101,
  TEST__IHK_DEVICE_MAP_VIRTUAL                    = 102,
  TEST__IHK_OS_ISSUE_INTERRUPT                    = 103,
  TEST__IHK_OS_GET_CPU_INFO                       = 104,
  TEST__IHK_OS_GET_MEMORY_INFO                    = 105,
  TEST_IHK_IKC_INIT_DESC                          = 106,
  TEST_IHK_HOST_FIND_OS                           = 107,
  TEST_MCCTRL_OS_BOOT_NOTIFIER                    = 108,
  TEST_SMP_IHK_OS_ISSUE_INTERRUPT                 = 109,
  TEST_GET_BASE_ENTRY                             = 110,
  TEST_IHK_OS_REGISTER_USER_CALL_HANDLERS         = 111,
  TEST_IHK_IKC_LISTEN_PORT                        = 112,
  TEST_IHK_IKC_SEND                               = 113,
  TEST_IHK_IKC_WRITE_QUEUE                        = 114,
  TEST_IHK_HOST_PRINT_OS_KMSG                     = 115,
  TEST_PREPARE_IKC_CHANNELS                       = 116,
  TEST_ADD_PROCFS_ENTRY                           = 117,
  TEST_SMP_IHK_OS_BOOT                            = 118,
  TEST_SMP_IHK_SETUP_TRAMPOLINE                   = 119,

  /* ihk_os_get_status */
  TEST__IHK_OS_QUERY_STATUS                       = 120,

  /* ihk_os_clear_kmsg */
  TEST__IHK_OS_CLEAR_KMSG                         = 121,

  /* ihk_os_kmsg */
  TEST__IHK_OS_READ_KMSG                          = 122,
  TEST_READ_KMSG                                  = 123,

  /* ihk_os_get_num_numa_nodes */
  TEST__IHK_OS_GET_NUM_NUMA_NODES                 = 124,

  /* ihk_os_getrusage */
  TEST_MCCTRL_GETRUSAGE                           = 125,

  /* ihk_os_setperfevent */
  TEST_MCCTRL_PERF_NUM                            = 126,
  TEST_MCCTRL_PERF_SET                            = 127,
  TEST_MCCTRL_IKC_SEND_WAIT                       = 128,
  TEST_MCCTRL_IKC_SEND                            = 129,
  TEST__MCCTRL_CONTROL                            = 130,
  TEST_SYSCALL_PACKET_HANDLER                     = 131,
  TEST_IHK_MC_MAP_VIRTUAL                         = 132,
  TEST_IHK_PAGEALLOC_ALLOC                        = 133,
  TEST__IHK_PAGEALLOC_LARGE                       = 134,
  TEST__SET_PT_PAGE                               = 135,
  TEST__IHK_MC_ALLOC_ALIGNED_PAGES_NODE           = 136,
  TEST___IHK_MC_ALLOC_ALIGNED_PAGES_NODE          = 137,
  TEST_VM_RANGE_POLICY_SEARCH                     = 138,
  TEST_IHK_MC_GET_NUMA_ID                         = 139,
  TEST_IHK_NUMA_ALLOC_PAGES                       = 140,
  TEST__PAGE_ALLOC_RBTREE_ALLOC_PAGES             = 141,
  TEST__PAGE_ALLOC_RBTREE_FREE_RANGE              = 142,
  TEST_RUSAGE_PAGE_ADD                            = 143,
  TEST_PROFILE_EVENT_ADD                          = 144,
  TEST__PAGEALLOC_TRACK_FIND_ENTRY                = 145,
  TEST__KMALLOC_CONSOLIDATE_LIST                  = 146,
  TEST_IHK_MC_UNMAP_VIRTUAL                       = 147,
  TEST__IHK_MC_PERFCTR_INIT                       = 148,
  TEST_IHK_IKC_RELEASE_PACKET                     = 149,
  TEST__CLEAR_PT_PAGE                             = 150,

  /* ihk_os_perfctl */
  TEST_MCCTRL_PERF_ENABLE                         = 151,
  TEST_MCCTRL_PERF_DISABLE                        = 152,

  /* ihk_os_getperfevent */
  TEST_MCCTRL_PERF_GET                            = 153,

  /* ihk_os_freeze */
  TEST__IHK_OS_FREEZE                             = 154,
  TEST_SMP_IHK_OS_SEND_MULTI_INTR                 = 155,
  TEST_IHK_SMP_SET_MULTI_INTR_MODE                = 156,
  TEST__IHK_OS_IOCTL_PERM                         = 157,
  TEST_IHK_HOST_OS_IOCTL                          = 158,

  /* ihk_os_thaw */
  TEST__IHK_OS_THAW                               = 159,

  /* ihk_os_makedumpfile */
  TEST__IHK_OS_DUMP                               = 160,
  TEST_GET_DUMP_NUM_MEM_AREAS                     = 161,

  /* ihk_os_read_cpu_register */
  TEST_IHK_OS_READ_CPU_REGISTER                   = 162,
  TEST__MCCTRL_OS_READ_WRITE_CPU_REGISTER         = 163,

  /* ihk_os_write_cpu_register */
  TEST_IHK_OS_WRITE_CPU_REGISTER                  = 164,

  /* ihk_get_request_os_cpu */
  TEST_IHK_GET_REQUEST_OS_CPU                     = 165,
  TEST_MCCTRL_GET_REQUEST_OS_CPU                  = 166,
  TEST_MCCTRL_GET_PER_THREAD_DATA                 = 167,
  TEST_MCCTRL_PUT_PER_THREAD_DATA                 = 168,
  TEST_MCCTRL_PUT_PER_THREAD_DATA_UNSAFE          = 169,
  TEST_MCCTRL_GET_PER_PROC_DATA                   = 170,
  TEST_MCCTRL_PUT_PER_PROC_DATA                   = 171,
  TEST__RETURN_SYSCALL                            = 172,

  // other API here
} ihk_test_mode_t;
extern int g_ihk_test_mode;

#define IHK_DEVICE_DEBUG_START        0x122900
#define IHK_DEVICE_DEBUG_END          0x1229ff

#define IHK_OS_LOAD                   0x112a00
#define IHK_OS_BOOT                   0x112a01
#define IHK_OS_SHUTDOWN               0x112a02
#define IHK_OS_QUERY_STATUS           0x112a03
#define IHK_OS_SET_KARGS              0x112a04
#define IHK_OS_QUERY_FREE_MEM         0x112a05
#define IHK_OS_DUMP                   0x112a06
#define IHK_OS_ALLOC_CPU              0x112a10
#define IHK_OS_ALLOC_MEM              0x112a11
#define IHK_OS_RESERVE_CPU            0x112a12
#define IHK_OS_RESERVE_MEM            0x112a13
#define IHK_OS_STATUS                 0x112a14
#define IHK_OS_REGISTER_EVENT         0x112a15
#define IHK_OS_EVENTFD                0x112a16
#define IHK_OS_WAIT_FOR_STATUS        0x112a17
#define IHK_OS_GET_STATUS             0x112a18
#define IHK_OS_FAKE_STATUS            0x112a19

#define IHK_OS_READ_KMSG              0x112a20
#define IHK_OS_CLEAR_KMSG             0x112a21
#define IHK_OS_PRINT_KMSG             0x112a40

#define IHK_OS_ASSIGN_CPU             0x112a22
#define IHK_OS_RELEASE_CPU            0x112a23
#define IHK_OS_ASSIGN_MEM             0x112a24
#define IHK_OS_RELEASE_MEM            0x112a25
#define IHK_OS_QUERY_CPU              0x112a26
#define IHK_OS_QUERY_MEM              0x112a27
#define IHK_OS_SET_IKC_MAP            0x112a28
#define IHK_OS_GET_IKC_MAP            0x112a29
#define IHK_OS_FREEZE                 0x112a30
#define IHK_OS_THAW                   0x112a31
#define IHK_OS_GET_USAGE              0x112a32
#define IHK_OS_GET_CPU_USAGE          0x112a33
#define IHK_OS_GET_NUM_NUMA_NODES     0x112a34
#define IHK_OS_NOTIFY_HUNGUP          0x112a35
#define IHK_OS_DETECT_HUNGUP          0x112a36
#define IHK_OS_GET_BUILDID            0x112a37
#define IHK_OS_GET_NUM_CPUS           0x112a38
#define IHK_OS_READ_KADDR             0x112a39

#define IHK_OS_DEBUG_START            0x122a00
#define IHK_OS_DEBUG_END              0x122aff

#define IHK_OS_AUX_CALL_START      0x10000000
#define IHK_OS_AUX_CALL_END        0x7fffffff

#define IHK_OS_AUX_PERF_NUM        0x11290100
#define IHK_OS_AUX_PERF_SET        0x11290101
#define IHK_OS_AUX_PERF_GET        0x11290102
#define IHK_OS_AUX_PERF_ENABLE     0x11290103
#define IHK_OS_AUX_PERF_DISABLE    0x11290104
#define IHK_OS_AUX_PERF_DESTROY    0x11290105
#define IHK_OS_GETRUSAGE           0x11290106
#define IHK_OS_READ_CPU_REGISTER   0x11290107
#define IHK_OS_WRITE_CPU_REGISTER  0x11290108
#define IHK_OS_SEND_NMI            0x11290109

#define FLAG_IHK_OS_SHUTDOWN_FORCE    0x40000000

#define PHYS_CHUNKS_DESC_SIZE 8192

struct dump_mem_chunk {
  unsigned long addr;
  unsigned long size;
};

typedef struct dump_mem_chunks_s {
  int nr_chunks;
  unsigned long kernel_base;
  /* memstart_addr in aarch64 */
  unsigned long phys_start;
  struct dump_mem_chunk chunks[];
} dump_mem_chunks_t;

typedef struct dumpargs_s {
  int cmd;
#define DUMP_NMI 1
#define DUMP_QUERY 2
#define DUMP_READ 3
#define DUMP_QUERY_ALL 4
#define DUMP_READ_ALL 5
#define DUMP_SET_LEVEL 6
#define DUMP_QUERY_NUM_MEM_AREAS 7
#define DUMP_QUERY_MEM_AREAS 8
#define DUMP_QUERY_PHYS_START 9
#define DUMP_NMI_CONT 10
  unsigned int level;
#define DUMP_LEVEL_ALL 0
#define DUMP_LEVEL_USER_UNUSED_EXCLUDE 24
  long start;
  long size;
  void *buf;
  void *spare[4];
} dumpargs_t;
#define DUMP_ALL_MEM 0
#define DUMP_CHUNK_MEM 24

typedef struct os_status_req {
  int status;
  int param_status;
  int cpu_status;
} os_status_req_t;

struct ihk_cpu_req {
  int *cpus;
  int num_cpus;
};

struct ihk_mem_req {
  size_t *sizes;
  int *numa_ids;
  int num_chunks;

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

struct ihk_ikc_req {
  int *src_cpus;  /* LWC CPUs as IKC source */
  int *dst_cpus;  /* Linux CPUs as IKC destination */
  int num_cpus;
};

/* Used by IHK-core and ihklib */
struct ihk_os_ioctl_eventfd_desc {
  int fd;
  enum ihk_os_eventfd_type type;
};

/* Used by mcinspect */
struct ihk_os_read_kaddr_desc {
  unsigned long kaddr;
  unsigned long len;
  void *ubuf;
};

/* Used by IHK-core and ihklib */
struct ihk_device_get_kmsg_buf_desc {
  int os_index; /* IN: OS index */
  void* handle; /* OUT: "Pointer" to kmsg_buf container */
};

/* Used by IHK-core and ihklib */
struct ihk_device_read_kmsg_buf_desc {
  void* handle; /* IN: "Pointer" to kmsg_buf container */
  int shift;    /* IN: Empty the buffer or not */
  char* buf;    /* OUT: Buffer */
};

#endif /* !defined(__HEADER_IHK_HOST_USER_H) */
