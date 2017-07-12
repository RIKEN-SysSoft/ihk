/**
 * \file ihk_host_user.h
 * \brief
 *	 IHK-Host: ioctl request numbers
 * \author Taku Shimosawa  <shimosawa@is.s.u-tokyo.ac.jp> \par
 * \author Balazs Gerofi  <bgerofi@riken.jp> \par
 * Copyright (C) 2011-2017 RIKEN AICS>
 */
#ifndef __HEADER_IHK_HOST_USER_H
#define __HEADER_IHK_HOST_USER_H

#include "ihk_os_status.h"

#define IHK_DEVICE_CREATE_OS          0x112900
#define IHK_DEVICE_DESTROY_OS         0x112901
#define IHK_DEVICE_RESERVE_CPU        0x112902
#define IHK_DEVICE_RELEASE_CPU        0x112903
#define IHK_DEVICE_RESERVE_MEM        0x112904
#define IHK_DEVICE_RELEASE_MEM        0x112905
#define IHK_DEVICE_QUERY_CPU          0x112906
#define IHK_DEVICE_QUERY_MEM          0x112907

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
#define IHK_OS_IKC_MAP                0x112a28
#define IHK_OS_QUERY_IKC_MAP          0x112a29
#define IHK_OS_FREEZE                 0x112a30
#define IHK_OS_THAW                   0x112a31
#define IHK_OS_GET_USAGE              0x112a32
#define IHK_OS_GET_CPU_USAGE          0x112a33

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
	int pad;
	long start;
	long size;
	void *buf;
	void *spare[4];
} dumpargs_t;
#define DUMP_ALL_MEM 0
#define DUMP_CHUNK_MEM 1

typedef struct ihk_resource_req_s {
	char *string;
	int string_len;
} ihk_resource_req_t;

int _ihklib_os_query_free_mem(int os_index, char *result, ssize_t sz_result);

#endif
