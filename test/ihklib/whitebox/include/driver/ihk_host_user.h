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
  TEST_SMP_IHK_OS_QUERY_STATUS                    = 33,
  TEST__IHK_OS_SEND_NMI                           = 34,
  TEST_SMP_IHK_OS_SEND_NMI                        = 35,
  TEST_IHK_OS_GET_SPECIAL_ADDR                    = 36,
  TEST_SMP_IHK_MAP_VIRTUAL                        = 37,
  TEST_IHK_SMP_MAP_VIRTUAL                        = 38,
  TEST_PAGER_CLEANUP                              = 39,
  TEST_SYSFSM_CLEANUP                             = 40,
  TEST_REMOVE                                     = 41,
  TEST_FREE_TOPOLOGY_INFO                         = 42,
  TEST_FREE_NODE_TOPOLOGY                         = 43,
  TEST_FREE_CPU_TOPOLOGY_ONE                      = 44,
  TEST_FREE_CPU_TOPOLOGY                          = 45,
  TEST_DESTROY_IKC_CHANNELS                       = 46,
  TEST_IHK_IKC_DESTROY_CHANNEL                    = 47,
  TEST_MCCTRL_WAKEUP_DESC_CLEANUP                 = 48,
  TEST_PROCFS_EXIT                                = 49,
  TEST_FIND_PROCFS_ENTRY                          = 50,
  TEST_DELETE_PROCFS_ENTRIES                      = 51,
  TEST_IKC_MASTER_FINALIZE                        = 52,
  TEST_IHK_IKC_FREE_CHANNEL                       = 53,
  TEST__IHK_DEVICE_UNMAP_VIRTUAL                  = 54,
  TEST_RELEASE_KMSG_BUF                           = 55,
  TEST_ADD_FREE_MEM_CHUNK                         = 56,
  TEST_IHK_SMP_CPU_KILL                           = 57,
  TEST_IHK_SMP_GET_CPU_AFFINITY                   = 58,
  TEST_SMP_IHK_OS_UNMAP_LWK                       = 59,
  TEST_IHK_SMP_RESET_CPU                          = 60,
  TEST_SMP_IHK_OS_SHUTDOWN                        = 61,
  TEST_IHK_SMP_SET_NMI_NODE                       = 62,

  /* ihk_os_assign_cpu */
  TEST_SMP_IHK_OS_ASSIGN_CPU                      = 63,
  TEST__IHK_OS_ASSIGN_CPU                         = 64,

  /* ihk_os_query_cpu */
  TEST_SMP_IHK_OS_QUERY_CPU                       = 65,
  TEST__IHK_OS_QUERY_CPU                          = 66,

  /* ihk_os_release_cpu */
  TEST__IHK_OS_RELEASE_CPU                        = 67,
  TEST_SMP_IHK_OS_RELEASE_CPU                     = 68,

  /* ihk_os_set_ikc_map */
  TEST__IHK_OS_SET_IKC_MAP                        = 69,
  TEST_SMP_IHK_OS_SET_IKC_MAP                     = 70,

  /* ihk_os_get_ikc_map */
  TEST__IHK_OS_GET_IKC_MAP                        = 71,
  TEST_SMP_IHK_OS_GET_IKC_MAP                     = 72,

  /* ihk_os_get_num_assigned_cpus */
  TEST__IHK_OS_GET_NUM_CPUS                       = 73,

  /* ihk_os_assign_mem */
  TEST__IHK_OS_ASSIGN_MEM                         = 74,
  TEST_MERGE_MEM_CHUNKS                           = 75,
  TEST_SMP_IHK_OS_ASSIGN_MEM                      = 76,
  TEST__SMP_IHK_OS_ASSIGN_MEM                     = 77,

  /* ihk_os_get_num_assigned_mem_chunks */
  TEST__IHK_OS_QUERY_MEM                          = 78,

  /* ihk_os_query_mem */
  TEST_SMP_IHK_OS_QUERY_MEM                       = 79,

  /* ihk_os_release_mem */
  TEST__IHK_OS_RELEASE_MEM                        = 80,
  TEST__SMP_IHK_OS_RELEASE_MEM                    = 81,
  TEST_SMP_IHK_OS_RELEASE_MEM                     = 82,

  /* ihk_os_get_eventfd */
  TEST__IHK_OS_REGISTER_EVENT                     = 83,

  /* ihk_os_load */
  TEST_SMP_IHK_ARCH_VMAP_AREA_TAKEN               = 84,
  TEST_SMP_IHK_OS_LOAD_MEM                        = 85,
  TEST__IHK_OS_LOAD_FILE                          = 86,
  TEST_SMP_IHK_OS_LOAD_FILE                       = 87,
  TEST__IHK_OS_LOAD_MEMORY                        = 88,

  /* ihk_os_kargs */
  TEST__IHK_OS_SET_KARGS                          = 89,

  /* ihk_os_boot */
  TEST__IHK_OS_BOOT                               = 90,
  TEST_LINUX_NUMA_2_LWK_NUMA                      = 91,
  TEST_SMP_WAKEUP_SECONDARY_CPU                   = 92,
  TEST_IHK_IKC_MASTER_INIT                        = 93,
  TEST_IHK_HOST_IKC_INIT_FIRST                    = 94,
  TEST__IHK_OS_REGISTER_HANDLER                   = 95,
  TEST__IHK_OS_GET_SPECIAL_ADDR                   = 96,
  TEST_IHK_DEVICE_MAP_MEMORY                      = 97,
  TEST__IHK_DEVICE_MAP_MEMORY                     = 98,
  TEST__IHK_DEVICE_MAP_VIRTUAL                    = 99,
  TEST__IHK_OS_ISSUE_INTERRUPT                    = 100,
  TEST__IHK_OS_GET_CPU_INFO                       = 101,
  TEST__IHK_OS_GET_MEMORY_INFO                    = 102,

  /* ihk_os_get_status */
  TEST__IHK_OS_QUERY_STATUS                       = 103,

  // other API here
} ihk_test_mode_t;
extern ihk_test_mode_t g_ihk_test_mode;

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

#define IHK_OS_READ_KMSG              0x112a20
#define IHK_OS_CLEAR_KMSG             0x112a21

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
