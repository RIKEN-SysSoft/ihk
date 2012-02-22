#ifndef KNF_AAL_MEMCONST_H
#define KNF_AAL_MEMCONST_H

#include <memory.h>

#define AAL_KMSG_SIZE      8192
#define AAL_KMSG_ALIGN     __attribute__((aligned(4096)))

#define AAL_DMA_ALIGN      __attribute__((aligned(64)))

#define AAL_PTA_REMOTE     PTATTR_UNCACHABLE

#endif

