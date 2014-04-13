/**
 * \file builtin_dma.h
 * \brief
 *	IHK BUILTIN Driver: Structures used by the BUILTIN DMA core
 * \author Taku Shimosawa  <shimosawa@is.s.u-tokyo.ac.jp> \par
 *	Copyright (C) 2011-2012 Taku Shimosawa <shimosawa@is.s.u-tokyo.ac.jp>
 */
#ifndef __HEADER_BUILTIN_DMA_H
#define __HEADER_BUILTIN_DMA_H

#ifdef USE_DMA

#include <linux/spinlock.h>

/* 32 byte */
/* MEMCPY: param2 (src), param3 (dest), param4 (len) */

#define BUILTIN_DMA_DESC_PARAM1_INTR  0x10000000

/** \brief Descriptor used by the BUILTIN DMA core */
struct builtin_dma_desc { 
	/** \brief Type of the request descriptor */
	int type;
	int param1;
	void *param2;
	void *param3;
	unsigned long param4;
};


#define BUILTIN_DMA_CHANNELS  2

/** \brief Structure for a DMA Channel of BUILTIN */
struct builtin_dma_channel {
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

struct builtin_dma_config_struct {
	/** \brief Array of the DMA channels of BUILTIN */
	struct builtin_dma_channel channels[BUILTIN_DMA_CHANNELS];

	/** \brief Doorbell for the DMA core */
	unsigned long doorbell;
	/** \brief Status of the DMA core */
	unsigned long status;
};

extern struct builtin_dma_config_struct *builtin_dma_config;
void builtin_dma_issue_interrupt(void);

void builtin_dma_desc_init(void);

#endif
#endif
