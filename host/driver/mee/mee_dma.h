/**
 * \file host/driver/mee/mee_dma.h
 * \brief Structures used by the MEE DMA core
 */
#ifndef __HEADER_MEE_DMA_H
#define __HEADER_MEE_DMA_H

#include <linux/spinlock.h>

/* 32 byte */
/* MEMCPY: param2 (src), param3 (dest), param4 (len) */

#define MEE_DMA_DESC_PARAM1_INTR  0x10000000

/** \brief Descriptor used by the MEE DMA core */
struct mee_dma_desc { 
	/** \brief Type of the request descriptor */
	int type;
	int param1;
	void *param2;
	void *param3;
	unsigned long param4;
};


#define MEE_DMA_CHANNELS  2

/** \brief Structure for a DMA Channel of MEE */
struct mee_dma_channel {
	/** \brief Physical address of the descriptor ring */
	unsigned long desc_ptr;
	/** \brief Number of descriptors in the ring */
	unsigned long len;
	/** \brief Head (writer) index in the descriptor ring */
	unsigned long head;
	/** \brief Tail (reader) index in the descriptor ring */
	unsigned long tail;

	/** \brief Lock for head, not tail (because single reader and writer
	 * for tail) */
	spinlock_t lock;
};

struct mee_dma_config_struct {
	/** \brief Array of the DMA channels of MEE */
	struct mee_dma_channel channels[MEE_DMA_CHANNELS];

	/** \brief Doorbell for the DMA core */
	unsigned long doorbell;
	/** \brief Status of the DMA core */
	unsigned long status;
};

extern struct mee_dma_config_struct *mee_dma_config;
void mee_dma_issue_interrupt(void);

void mee_dma_desc_init(void);

#endif
