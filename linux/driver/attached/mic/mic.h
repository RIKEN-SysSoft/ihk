/**
 * \file mic.h
 *  License details are found in the file LICENSE.
 * \brief
 *	IHK MIC Driver: Header file for miscellaneous structures used
 *                        in Xeon Phi drivers
 * \author Taku Shimosawa  <shimosawa@is.s.u-tokyo.ac.jp> \par
 *	Copyright (C) 2011-2012 Taku Shimosawa
 */
#ifndef MIC_H
#define MIC_H
#include <ihk/ihk_host_driver.h>
#include <sysdeps/mic/mic_host.h>

#define MIC_DMA_CHANNELS 8

/** \brief Descriptor for DMA channels of Knights Ferry */
struct mic_dma_channel {
	struct mic_device_data *kdd;
 	/** \brief Lock of this structure */  
	spinlock_t lock;
	/** \brief Head index of the DMA ring */  
	int head;
	/** \brief Cached tail index of the DMA ring */  
	int tail;
	/** \brief Channel number*/  
	int channel;
	/** \brief Number of DMA request descriptors in the DMA ring */  
	int desc_count;
 	/** \brief Owner (0 = MIC / 1 = Host) */  
	int owner;
 	/** \brief Pointer to the DMA ring */  
	union md_mic_dma_desc *desc;
};

/** \brief MIC driver-private data structure for Knights Ferry devices */
struct mic_device_data {
	/** \brief IHK Device Structure */
	ihk_device_t ihk_dev;
	/** \brief PCI Device */
	struct pci_dev *dev;
	/** \brief Lock */
	spinlock_t lock;
	/** \brief Status of the Knights Ferry device */
	int status;
	/** \brief Descriptor for an memory allocator of the aperture
	 *  area. */
	void *alloc_desc;

	/** \brief Physical address of the aperture */
	unsigned long aperture_pa;
	/** \brief Size of the aperture */
	unsigned long aperture_len;
	/** \brief Linux kernel virtual address of the aperture */
	void *aperture_va;
	/** \brief Physical address of the MMIO area */
	unsigned long mmio_pa;
	/** \brief Size of the MMIO area */
	unsigned long mmio_len;
	/** \brief Linux kernel virtual address of the MMIO area */
	void *mmio_va;

	/** \brief IRQ number of the interrupts from the Knights Ferry device */
	int irq;
	/** \brief Starting physical address in the device memory
	 * where the kernel image should be put */
	unsigned long os_load_offset;
	/** \brief APIC ID of the BSP of the Knights Ferry device */
	unsigned int bsp_apic_id;

	/** \brief IHK-version of the descriptors of DMA channels */
	struct ihk_dma_channel ihk_channels[MIC_DMA_CHANNELS];
	/** \brief Driver-private version of the descriptors of DMA channels */
	struct mic_dma_channel channels[MIC_DMA_CHANNELS];

	/** \brief IHK Memory information structure */
	struct ihk_mem_info mem_info;
	/** \brief IHK CPU information structure */
	struct ihk_cpu_info cpu_info;
	/** \brief IHK Memory region information structure */
	struct ihk_mem_region mem_region;

	/** \brief List of the APIC IDs of the cores in the device */
	int cpu_hw_ids[512];
};

#define MICDD_STATUS_READY          1
#define MICDD_STATUS_LOADING        2
#define MICDD_STATUS_BOOTING        3
#define MICDD_STATUS_RUNNING        4

#define MICDD_OS_STATUS_NONE        0
#define MICDD_OS_STATUS_LOADED      1

/** \brief MIC driver-private data structure for OS instances */
struct mic_os_data {
	/** \brief Pointer to the structure of the device */
	struct mic_device_data *dev;
	/** \brief Lock for this structure */
	spinlock_t lock;

	/** \brief Status of the OS instance */
	atomic_t status;
	/** \brief APIC ID of the BSP */
	int bspid;

	/** \brief Boot parameter for the OS instance */
	struct mic_boot_param boot_param;
};

#endif
