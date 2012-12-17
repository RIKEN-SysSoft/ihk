#ifndef MIC_IHK_MEMCONST_H
#define MIC_IHK_MEMCONST_H

#include <memory.h>

#define IHK_KMSG_SIZE	   (12 * 1024)
#define IHK_KMSG_ALIGN     __attribute__((aligned(4096)))

#define IHK_DMA_ALIGN      __attribute__((aligned(64)))

#define IHK_PTA_REMOTE     PTATTR_UNCACHABLE

#endif

