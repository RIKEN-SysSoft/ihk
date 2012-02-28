#ifndef __HEADER_AAL_DRIVER_MIC_H
#define __HEADER_AAL_DRIVER_MIC_H

#include "knf.h"
#include <sysdeps/knf/mic/micconst.h>
#include <sysdeps/knf/mic/micsboxdefine.h>
#include <sysdeps/knf/mic/mic_dma.h>

static unsigned int knf_read_sbox(struct knf_device_data *kdd, int offset)
{
	return readl((unsigned int *)((char *)(kdd->mmio_va) + 
	                              MMIO_SBOX_BASE_OFFSET + offset));
}

static void knf_write_sbox(struct knf_device_data *kdd, int offset,
                           unsigned int value)
{
	writel(value, (unsigned int *)((char *)(kdd->mmio_va) + 
	                               MMIO_SBOX_BASE_OFFSET + offset));
}

#endif
