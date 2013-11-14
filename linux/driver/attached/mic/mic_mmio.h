/**
 * \file mic_mmio.h
 *  License details are found in the file LICENSE.
 * \brief
 *	IHK MIC Driver: MIC-related functions
 * \author Taku Shimosawa  <shimosawa@is.s.u-tokyo.ac.jp> \par
 *	Copyright (C) 2011-2012 Taku Shimosawa
 */
#ifndef __HEADER_IHK_DRIVER_MIC_H
#define __HEADER_IHK_DRIVER_MIC_H

#include "mic.h"
#include <sysdeps/mic/mic/micconst.h>
#include <sysdeps/mic/mic/micsboxdefine.h>
#include <sysdeps/mic/mic/mic_dma.h>

/** \brief Read a register in the SBOX MMIO area.
 *
 * @param kdd   A Knights Ferry device
 * @param offset Offset of the register in SBOX to read
 * @return Value of the register
 */
static unsigned int mic_read_sbox(struct mic_device_data *kdd, int offset)
{
	return readl((unsigned int *)((char *)(kdd->mmio_va) + 
	                              MMIO_SBOX_BASE_OFFSET + offset));
}

/** \brief Write a register in the SBOX MMIO area.
 *
 * @param kdd   A Knights Ferry device
 * @param offset Offset of the register in SBOX to write
 * @param value Value to write
 */
static void mic_write_sbox(struct mic_device_data *kdd, int offset,
                           unsigned int value)
{
	writel(value, (unsigned int *)((char *)(kdd->mmio_va) + 
	                               MMIO_SBOX_BASE_OFFSET + offset));
}

#endif
