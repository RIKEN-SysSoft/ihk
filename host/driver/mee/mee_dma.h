#ifndef __HEADER_MEE_DMA_H
#define __HEADER_MEE_DMA_H

#include <linux/spinlock.h>

/* 32 byte */
/* MEMCPY: param2 (src), param3 (dest), param4 (len) */

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
	struct mee_dma_desc *desc_ptr;
	unsigned long len;
	unsigned long head;
	unsigned long tail;

	/* Lock for head, not tail (because single r/w for tail) */
	spinlock_t lock;
};

struct mee_dma_config_struct {
	struct mee_dma_channel channels[MEE_DMA_CHANNELS];

	unsigned long doorbell; /* doorbell */
	unsigned long status; /* core status */
};

extern struct mee_dma_config_struct mee_dma_config;
void mee_dma_issue_interrupt(void);

void mee_dma_desc_init(void);

#endif
