/**
 * \file host/driver/knf/knf.h
 * \brief AAL KNF Driver: Header file for miscellaneous structures used
 *                        in Knights Ferry drivers
 */
#ifndef KNF_H
#define KNF_H
#include <aal/aal_host_driver.h>
#include <sysdeps/knf/mic/mic_type.h>
#include <sysdeps/knf/knf_host.h>

#define KNF_DMA_CHANNELS 8

/** \brief Descriptor for DMA channels of Knights Ferry */
struct knf_dma_channel {
	struct knf_device_data *kdd;
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

/** \brief KNF driver-private data structure for Knights Ferry devices */
struct knf_device_data {
	/** \brief AAL Device Structure */
	aal_device_t aal_dev;
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

	/** \brief AAL-version of the descriptors of DMA channels */
	struct aal_dma_channel aal_channels[KNF_DMA_CHANNELS];
	/** \brief Driver-private version of the descriptors of DMA channels */
	struct knf_dma_channel channels[KNF_DMA_CHANNELS];

	/** \brief AAL Memory information structure */
	struct aal_mem_info mem_info;
	/** \brief AAL CPU information structure */
	struct aal_cpu_info cpu_info;
	/** \brief AAL Memory region information structure */
	struct aal_mem_region mem_region;

	/** \brief List of the APIC IDs of the cores in the device */
	int cpu_hw_ids[512];
};

#define KNFDD_STATUS_READY          1
#define KNFDD_STATUS_LOADING        2
#define KNFDD_STATUS_BOOTING        3
#define KNFDD_STATUS_RUNNING        4

#define KNFDD_OS_STATUS_NONE        0
#define KNFDD_OS_STATUS_LOADED      1

/** \brief KNF driver-private data structure for OS instances */
struct knf_os_data {
	/** \brief Pointer to the structure of the device */
	struct knf_device_data *dev;
	/** \brief Lock for this structure */
	spinlock_t lock;

	/** \brief Status of the OS instance */
	atomic_t status;
	/** \brief APIC ID of the BSP */
	int bspid;

	/** \brief Boot parameter for the OS instance */
	struct knf_boot_param boot_param;
};

#endif
