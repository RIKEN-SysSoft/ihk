/**
 * \file ihk_host_user.h
 * \brief
 *	 IHK-Host: ioctl request numbers
 * \author Taku Shimosawa  <shimosawa@is.s.u-tokyo.ac.jp> \par
 * Copyright (C) 2011-2012 Taku Shimosawa <shimosawa@is.s.u-tokyo.ac.jp>
 */
#ifndef __HEADER_IHK_HOST_USER_H
#define __HEADER_IHK_HOST_USER_H

#define IHK_DEVICE_CREATE_OS          0x112900
#define IHK_DEVICE_DESTROY_OS         0x112901

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

#define IHK_OS_READ_KMSG              0x112a20
#define IHK_OS_CLEAR_KMSG             0x112a21

#define IHK_OS_DEBUG_START            0x122a00
#define IHK_OS_DEBUG_END              0x122aff

#define IHK_OS_AUX_CALL_START      0x10000000
#define IHK_OS_AUX_CALL_END        0x7fffffff

#define FLAG_IHK_OS_SHUTDOWN_FORCE    0x40000000

typedef struct dumpargs_s {
	int cmd;
#define DUMP_NMI 1
#define DUMP_QUERY 2
#define DUMP_READ 3
	int pad;
	long start;
	long size;
	void *buf;
	void *spare[4];
} dumpargs_t;

#endif
