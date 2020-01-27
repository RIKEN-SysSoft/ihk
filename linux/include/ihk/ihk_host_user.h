/**
 * \file ihk_host_user.h
 * \brief
 *	 IHK-Host: ioctl request numbers
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

	/* Stop gathering chunks for "all" request after accumulating
	 * this percentage
	 */
	int all_size_limit;

	/* Give up proceeding to the smaller order when it took longer
	 * than this seconds for the current order
	 */
	int timeout;
};

struct ihk_ikc_req {
	int *src_cpus;	/* LWC CPUs as IKC source */
	int *dst_cpus;	/* Linux CPUs as IKC destination */
	int num_cpus;
};

/* Used by IHK-core and ihklib */
struct ihk_os_ioctl_eventfd_desc {
	int fd;
	enum ihk_os_eventfd_type type;
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
