#ifndef __HEADER_AAL_HOST_USER_H
#define __HEADER_AAL_HOST_USER_H

#define AAL_DEVICE_CREATE_OS          0x112900

#define AAL_DEVICE_DEBUG_START        0x122900
#define AAL_DEVICE_DEBUG_END          0x1229ff

#define AAL_OS_LOAD                   0x112a00
#define AAL_OS_BOOT                   0x112a01
#define AAL_OS_SHUTDOWN               0x112a02

#define AAL_OS_DEBUG_START            0x122a00
#define AAL_OS_DEBUG_END              0x122aff

#define FLAG_AAL_OS_SHUTDOWN_FORCE    0x40000000
#endif
