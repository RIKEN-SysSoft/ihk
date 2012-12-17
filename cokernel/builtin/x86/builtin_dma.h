#ifndef __HEADER_MEE_DMA_H
#define __HEADER_MEE_DMA_H

#include <aal/lock.h>

#define MEE_DMA_DESC_PARAM1_INTR  0x10000000

struct mee_dma_desc { 
	int type;
	int param1;
	void *param2;
	void *param3;
	unsigned long param4;
};

#define MEE_DMA_CHANNELS  2

struct mee_dma_channel {
	unsigned long desc_ptr;
	unsigned long len;
	unsigned long head;
	unsigned long tail;

	/* Lock for head, not tail (because single r/w for tail) */
	aal_spinlock_t lock;
};

struct mee_dma_config_struct {
	struct mee_dma_channel channels[MEE_DMA_CHANNELS];

	unsigned long doorbell; /* doorbell */
	unsigned long status; /* core status */
};

#endif
