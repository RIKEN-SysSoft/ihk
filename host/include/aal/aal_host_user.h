/**
 * \file aal_host_user.h
 * \brief AAL-Host: ioctl request numbers
 *
 * Copyright (C) 2011-2012 Taku Shimosawa <shimosawa@is.s.u-tokyo.ac.jp>
 */
#ifndef __HEADER_AAL_HOST_USER_H
#define __HEADER_AAL_HOST_USER_H

#define AAL_DEVICE_CREATE_OS          0x112900

#define AAL_DEVICE_DEBUG_START        0x122900
#define AAL_DEVICE_DEBUG_END          0x1229ff

#define AAL_OS_LOAD                   0x112a00
#define AAL_OS_BOOT                   0x112a01
#define AAL_OS_SHUTDOWN               0x112a02
#define AAL_OS_QUERY_STATUS           0x112a03
#define AAL_OS_SET_KARGS              0x112a04
#define AAL_OS_ALLOC_CPU              0x112a10
#define AAL_OS_ALLOC_MEM              0x112a11
#define AAL_OS_RESERVE_CPU            0x112a12
#define AAL_OS_RESERVE_MEM            0x112a13

#define AAL_OS_READ_KMSG              0x112a20
#define AAL_OS_CLEAR_KMSG             0x112a21

#define AAL_OS_DEBUG_START            0x122a00
#define AAL_OS_DEBUG_END              0x122aff

#define AAL_OS_AUX_CALL_START      0x10000000
#define AAL_OS_AUX_CALL_END        0x7fffffff

#define FLAG_AAL_OS_SHUTDOWN_FORCE    0x40000000
#endif
