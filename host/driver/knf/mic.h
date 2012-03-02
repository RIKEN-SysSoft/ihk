#ifndef __HEADER_AAL_DRIVER_MIC_H
#define __HEADER_AAL_DRIVER_MIC_H

#include "knf.h"
#include <sysdeps/knf/mic/micconst.h>
#include <sysdeps/knf/mic/micsboxdefine.h>
#include <sysdeps/knf/mic/mic_dma.h>

/** \brief Read a register in the SBOX MMIO area.
 *
 * @param kdd   A Knights Ferry device
 * @param offset Offset of the register in SBOX to read
 * @return Value of the register
 */
static unsigned int knf_read_sbox(struct knf_device_data *kdd, int offset)
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
static void knf_write_sbox(struct knf_device_data *kdd, int offset,
                           unsigned int value)
{
	writel(value, (unsigned int *)((char *)(kdd->mmio_va) + 
	                               MMIO_SBOX_BASE_OFFSET + offset));
}

#endif
