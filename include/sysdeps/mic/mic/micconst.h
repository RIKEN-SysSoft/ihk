/**
 * \file micconst.h
 * Licence details are found in the file LICENSE.
 *  
 * \brief
 * Constans values for KNF/KNC, taken from MPSS Linux
 *
 * \author Taku Shimosawa  <shimosawa@is.s.u-tokyo.ac.jp> \par
 * Copyright (C) 2011 - 2012  Taku Shimosawa
 * 
 * \author Balazs Gerofi  <bgerofi@riken.jp> \par
 * Copyright (C) 2012  RIKEN AICS
 */
#ifndef MICCONST_H
#define MICCONST_H

#include "mic_type.h"

/* /host/driver/mic_common.h */
#define DLDR_APT_BAR 0
#define DLDR_MMIO_BAR 4

#define MMIO_DBOX_BASE_OFFSET       0x00000000
#define MMIO_SBOX_BASE_OFFSET       0x00010000
#define MMIO_GTT_BASE_OFFSET        0x00040000

#define SCRATCH2_DOWNLOAD_ADDR(x)   ((x) & 0xfffff000)
#define SCRATCH2_DOWNLOAD_STATUS(x) ((x) & 0x1)
#define SCRATCH2_APIC_ID(x)     (((x) >> 1) & 0x1ff)

#define SBOX_SICE0_DBR(x)       ((x) & 0xf)
#define SBOX_SICE0_DBR_BITS(x)      ((x) & 0xf)
#define SBOX_SICE0_DMA(x)       (((x) >> 8) & 0xff)
#define SBOX_SICE0_DMA_BITS(x)      (((x) & 0xff) << 8)

#define MIC_DBR_ALL_MASK            0xf
#define MIC_DMA_ALL_MASK            0xff

/* (mic side) */
#define SBOX_BASE               0x08007D0000ULL
#define SBOX_SIZE               0x30000ULL
#define MIC_GTT_BASE            0x0800800000ULL

/* /host/driver/uos_download.c */
#define MIC_DMA_INTERRUPT_VECTOR 229
#define MIC_ICR_INTVEC_SHIFT 0

/* /card/driver/include/mic/micscif_smpt.h */
#define SNOOP_ON  (0 << 0)
#define SNOOP_OFF (1 << 0)

#ifdef CONFIG_MIC
#define NUM_SMPT_REGISTERS 32
#define BUILD_SMPT(NO_SNOOP, HOST_ADDR)  \
	(uint32_t)(((((HOST_ADDR)<< 2) & (~0x03)) | ((NO_SNOOP) & (0x01))))
#else
#define	NUM_SMPT_ENTRIES_IN_USE		32
#define	NUM_SMPT_ENTRIES_MICPA	4 /* used by ihk_mc_map_micpa */
#define MIC_SYSTEM_PAGE_SIZE	0x0400000000ULL
#define BUILD_SMPT(NO_SNOOP, HOST_ADDR)  \
	(uint32_t)(((((HOST_ADDR)<< 2) & (~0x03)) | ((NO_SNOOP) & (0x01))))
#endif

#define SMPT_MASK 0x1F
#define MIC_SYSTEM_PAGE_SHIFT 34ULL
#define MIC_SYSTEM_PAGE_MASK ((1ULL << MIC_SYSTEM_PAGE_SHIFT) - 1ULL)

#define MIC_SYSTEM_BASE     0x8000000000ULL

#define MIC_DMA_CHANNELS            8

#endif /* MICCONST_H */
