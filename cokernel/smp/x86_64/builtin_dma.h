#ifndef __HEADER_BUILTIN_DMA_H
#define __HEADER_BUILTIN_DMA_H

#include <ihk/lock.h>

#define BUILTIN_DMA_DESC_PARAM1_INTR  0x10000000

struct builtin_dma_desc { 
	int type;
	int param1;
	void *param2;
	void *param3;
	unsigned long param4;
};

#define BUILTIN_DMA_CHANNELS  2

struct builtin_dma_channel {
	unsigned long desc_ptr;
	unsigned long len;
	unsigned long head;
	unsigned long tail;

	/* Lock for head, not tail (because single r/w for tail) */
	ihk_spinlock_t lock;
};

struct builtin_dma_config_struct {
	struct builtin_dma_channel channels[BUILTIN_DMA_CHANNELS];

	unsigned long doorbell; /* doorbell */
	unsigned long status; /* core status */
};

#endif
