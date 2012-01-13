#ifndef KNF_H
#define KNF_H
#include <aal/aal_host_driver.h>

#define KNF_DMA_CHANNELS 8

struct knf_dma_channel {
	struct knf_device_data *kdd;
	spinlock_t lock;
	int head, tail;
	int channel;
	int desc_count;
	int owner;
	union md_mic_dma_desc *desc;
};

struct knf_device_data {
	aal_device_t aal_dev;
	struct pci_dev *dev;
	spinlock_t lock;
	int status;
	void *alloc_desc;

	unsigned long aperture_pa, aperture_len;
	void *aperture_va;
	unsigned long mmio_pa, mmio_len;
	void *mmio_va;

	int irq;
	unsigned long os_load_offset;
	unsigned int bsp_apic_id;

	struct aal_dma_channel aal_channels[KNF_DMA_CHANNELS];
	struct knf_dma_channel channels[KNF_DMA_CHANNELS];
};

#define KNFDD_STATUS_READY          1
#define KNFDD_STATUS_LOADING        2
#define KNFDD_STATUS_BOOTING        3
#define KNFDD_STATUS_RUNNING        4

#define KNFDD_OS_STATUS_NONE        0
#define KNFDD_OS_STATUS_LOADED      1

struct knf_os_data {
	struct knf_device_data *dev;
	spinlock_t lock;

	int os_status;
	atomic_t status;

	int bspid;
};

#endif
