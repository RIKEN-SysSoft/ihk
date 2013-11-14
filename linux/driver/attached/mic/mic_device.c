/**
 * \file mic_device.c
 *  License details are found in the file LICENSE.
 * \brief
 *	IHK MIC Driver: MIC Driver Core
 * \author Taku Shimosawa  <shimosawa@is.s.u-tokyo.ac.jp> \par
 * Copyright (C) 2011-2012 Taku Shimosawa <shimosawa@is.s.u-tokyo.ac.jp>
 */
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/interrupt.h>
#include <ihk/misc/debug.h>
#include <ikc/msg.h>

#include "mic_user.h"
#include "mic_mmio.h"

void mic_device_destroy(struct pci_dev *dev, struct mic_device_data *data);

static void load_scratch_values(struct mic_device_data *kdd);
void mic_shutdown(struct mic_device_data *kdd);
static irqreturn_t mic_irq_handler(int irq, void *data);

static void mic_enable_interrupts(struct mic_device_data *kdd,
                                  int intr_mask, int dma_mask);
static void mic_disable_interrupts(struct mic_device_data *kdd,
                                   int intr_mask, int dma_mask);
static void mic_write_sbox(struct mic_device_data *kdd, int offset,
                           unsigned int value);

void __mic_dma_init(struct mic_device_data *kdd);
void __mic_dma_finalize(struct mic_device_data *kdd);
int __mic_dma_test(struct mic_device_data *kdd, unsigned long arg);
void __mic_reset_dma_registers(struct mic_device_data *kdd);

/**
 * \brief Initialize the specified Knights Ferry board.
 *
 * \param dev  A pci_dev structure corresponding to the device to initialize
 * \param kdd An internal data structure in this driver.
 */
int mic_device_init(struct pci_dev *dev, struct mic_device_data *kdd)
{
	int i;
	int err = 0;

	if ((err = pci_enable_device(dev)) < 0) {
		return err;
	}
	pci_set_master(dev);
	if ((err = pci_reenable_device(dev)) < 0) {
		return err;
	}
	if ((err = pci_set_dma_mask(dev, DMA_BIT_MASK(64))) < 0) {
		return err;
	}

	kdd->aperture_pa = pci_resource_start(dev, DLDR_APT_BAR);
	kdd->aperture_len = pci_resource_len(dev, DLDR_APT_BAR);
	kdd->mmio_pa = pci_resource_start(dev, DLDR_MMIO_BAR);
	kdd->mmio_len = pci_resource_len(dev, DLDR_MMIO_BAR);

	printk("MIC aperture len: %lu, mmio lenght: %lu\n", 
	       kdd->aperture_len, kdd->mmio_len);

	if (!request_mem_region(kdd->aperture_pa, kdd->aperture_len, "mic")) {
		err = -ENOMEM;
		goto FIN;
	}

	if (!request_mem_region(kdd->mmio_pa, kdd->mmio_len, "mic")) {
		release_mem_region(kdd->aperture_pa, kdd->aperture_len);
		err = -ENOMEM;
		goto FIN;
	}

	if (!(kdd->mmio_va = ioremap_nocache(kdd->mmio_pa, kdd->mmio_len))) {
		mic_device_destroy(dev, kdd);
		return -ENOMEM;
	}	
	                                   
	if (!(kdd->aperture_va = ioremap_wc(kdd->aperture_pa,
	                                    kdd->aperture_len))) {
		mic_device_destroy(dev, kdd);
		return -ENOMEM;
	}

	load_scratch_values(kdd);

	dprint_var_x8(kdd->mmio_pa);
	dprint_var_x8(kdd->mmio_len);
	dprint_var_p(kdd->mmio_va);
	dprint_var_x8(kdd->aperture_pa);
	dprint_var_x8(kdd->aperture_len);
	dprint_var_p(kdd->aperture_va);
	dprint_var_p(kdd);

	if (request_irq(dev->irq, mic_irq_handler, IRQF_SHARED, "mic", kdd)
	    != 0) {
		dprintf("Failed to request irq");
		mic_device_destroy(dev, kdd);
		return -ENOMEM;
	}
	kdd->irq = dev->irq;

	__mic_dma_init(kdd);

#ifdef CONFIG_MIC
	/* XXX: This should be dynamically obtained from the card */
	kdd->mem_region.start = 0;
	kdd->mem_region.size = 0x80000000;
	kdd->mem_info.n_mappable = kdd->mem_info.n_available = 1;
	kdd->mem_info.n_fixed = 0;
	kdd->mem_info.available = kdd->mem_info.mappable = &kdd->mem_region;
	kdd->mem_info.fixed = NULL;
#endif
	
	/* Currently BSP id + 4 is number of cores */
	kdd->cpu_info.n_cpus = kdd->bsp_apic_id + 4; 
	kdd->cpu_info.hw_ids = kdd->cpu_hw_ids;

	/* TODO: do this in a more generic way! */
	for (i = 0; i < kdd->cpu_info.n_cpus; i++) {
		kdd->cpu_hw_ids[i] = i;
	}

	mic_enable_interrupts(kdd, MIC_DBR_ALL_MASK, MIC_DMA_ALL_MASK);

	return 0;

FIN:
	pci_disable_device(dev);
	return err;
}

/**
 * \brief Stops the specified device and destroy the related structures.
 *
 * \param dev  A pci_dev structure to be destroyed
 * \param kdd  An internal data structure for the driver.
 */
void mic_device_destroy(struct pci_dev *dev, struct mic_device_data *kdd)
{
	dprint_func_enter;
	dprint_var_p(dev);
	dprint_var_p(kdd);

	mic_disable_interrupts(kdd, MIC_DBR_ALL_MASK, MIC_DMA_ALL_MASK);
	if (kdd->irq) {
		free_irq(kdd->irq, kdd);
	}

	__mic_dma_finalize(kdd);

	if (kdd->aperture_va)
		iounmap(kdd->aperture_va);
	if (kdd->mmio_va)
		iounmap(kdd->mmio_va);

	release_mem_region(kdd->aperture_pa, kdd->aperture_len);
	release_mem_region(kdd->mmio_pa, kdd->mmio_len);
	pci_disable_device(dev);
}

#ifdef CONFIG_MIC
/** \brief Get an entry in GTT.
 *
 * @param kdd  An interal structure for a Knights Ferry device
 * @param entry Index in the GTT to query (0 to GTT_SIZE / 4)
 */
static unsigned int get_gtt_entry(struct mic_device_data *kdd, int entry)
{
	return readl((unsigned int *)((char *)(kdd->mmio_va) + 
	                              MMIO_GTT_BASE_OFFSET + 4 * entry));
}

/** \brief Set an entry in GTT.
 *
 * @param kdd  A Knights Ferry device
 * @param entry Index in the GTT to set (0 to GTT_SIZE / 4)
 * @param phys Physical address in Knights Ferry to be mapped to the GTT entry
 * @param enable Whether the mapping is enabled or not
 */
static void set_gtt_entry(struct mic_device_data *kdd, int entry,
                          unsigned long phys, unsigned int enable)
{
	writel((unsigned int)((phys >> PAGE_SHIFT) << 1) | enable,
	       (unsigned int *)((char *)(kdd->mmio_va) + 
	                        MMIO_GTT_BASE_OFFSET + 4 * entry));
}
#endif

/** \brief Maps an memory area to the aperture in a Knights Ferry device.
 *
 * @param kdd        A Knights Ferry device
 * @param ap_address Start address in the aperture to be mapped to.
 * @param phys       Start physical address in Knights Ferry to be mapped.
 * @param napges     Number of pages to map
 */
int mic_map_aperture(struct mic_device_data *kdd, unsigned long ap_address,
                     unsigned long phys, int npages)
{
#ifdef CONFIG_MIC
	int i = (int)((ap_address - kdd->aperture_pa) >> PAGE_SHIFT);
	int e = i + npages;
	unsigned long flags;

	dprintf("mic: map_aperture %lx - %lx => %lx\n", ap_address,
	        ap_address + (npages << PAGE_SHIFT), phys);

	spin_lock_irqsave(&kdd->lock, flags);
	for (; i < e; i++) {
		set_gtt_entry(kdd, i, phys, 1);
		phys += PAGE_SIZE;
	}
	spin_unlock_irqrestore(&kdd->lock, flags);
#endif

	smp_mb();

	mic_write_sbox(kdd, SBOX_SBQ_FLUSH, 1);
	mic_write_sbox(kdd, SBOX_TLB_FLUSH, 1);

	return 0;
}

/** \brief Unmaps an memory area to the aperture in a Knights Ferry device.
 *
 * @param kdd        A Knights Ferry device
 * @param ap_address Start address in the aperture to unmap
 * @param napges     Number of pages to unmap
 */
int mic_unmap_aperture(struct mic_device_data *kdd, unsigned long ap_address,
                       int npages)
{
#ifdef CONFIG_MIC
	int i = (int)((ap_address - kdd->aperture_pa) >> PAGE_SHIFT);
	int e = i + npages;
	unsigned long flags;

	dprintf("mic: unmap_aperture %lx - %lx\n", ap_address,
	        ap_address + (npages << PAGE_SHIFT));

	spin_lock_irqsave(&kdd->lock, flags);
	for (; i < e; i++) {
		set_gtt_entry(kdd, i, 0, 0);
	}
	spin_unlock_irqrestore(&kdd->lock, flags);
#endif	

	return 0;
}

int __mic_prepare_os_load(struct mic_device_data *kdd);

/** \brief Shuts down a Knights Ferry device */
void mic_shutdown(struct mic_device_data *kdd)
{
	unsigned int reset;

	dprint_func_enter;

	mic_write_sbox(kdd, SBOX_SCRATCH2, 0);	
	mic_write_sbox(kdd, SBOX_SCRATCH12, 0);
	mic_write_sbox(kdd, SBOX_SCRATCH13, 0);
	mic_write_sbox(kdd, SBOX_SCRATCH14, 0);
#ifdef CONFIG_MIC
	mic_write_sbox(kdd, SBOX_SCRATCH15, 0);
#endif

	reset = mic_read_sbox(kdd, SBOX_RGCR);
	reset = 1;
	mic_write_sbox(kdd, SBOX_RGCR, reset);

	msleep(1000);

	__mic_prepare_os_load(kdd);
}

/** \brief Sets up scratch values in Knights Ferry for booting */
static void load_scratch_values(struct mic_device_data *kdd)
{
	unsigned long scratch2;

	scratch2 = mic_read_sbox(kdd, SBOX_SCRATCH2);

	kdd->os_load_offset = SCRATCH2_DOWNLOAD_ADDR(scratch2);
	kdd->bsp_apic_id = SCRATCH2_APIC_ID(scratch2);

	dprint_var_x8(kdd->os_load_offset);
	dprint_var_i4(kdd->bsp_apic_id);
}

/** \brief Timeout for __wait_for_bootstrap_ready in 100-ms unit */
#define WFBR_TIMEOUT 1000

/** \brief Waits for the Knights Ferry device to get ready by spinning */
static int __wait_for_bootstrap_ready(struct mic_device_data *kdd)
{
	unsigned int scratch2;
	int count;

	for (count = 0; count < WFBR_TIMEOUT; count++) {
		scratch2 = mic_read_sbox(kdd, SBOX_SCRATCH2);
		
		if (SCRATCH2_DOWNLOAD_STATUS(scratch2)) 
			return 0;

		msleep(100);
	}

	dprintf("%s : Timed out (%d * 100ms)\n", __FUNCTION__, count);
	return -EBUSY;
}

/** \brief Initializes the Knights Ferry device to boot
 *
 * This function sets up the GTT entries to map straightly starting from
 * the load offset (where the kernel image should be loaded),
 * and prepares some registers.
 */
int __mic_prepare_os_load(struct mic_device_data *kdd)
{
#ifdef CONFIG_MIC
	unsigned long i, phys;
#endif

	if (__wait_for_bootstrap_ready(kdd) < 0) {
		return -EBUSY;
	}

#ifdef CONFIG_MIC
	dprint_var_x8(kdd->os_load_offset);
	phys = kdd->os_load_offset;

	for (i = 0; i < (kdd->aperture_len >> PAGE_SHIFT); i++) {
		set_gtt_entry(kdd, i, phys, 1);

		phys += PAGE_SIZE;
	}
#endif	

	smp_mb();

	//mic_write_sbox(kdd, SBOX_PCIE_BAR_ENABLE, 3);
	mic_write_sbox(kdd, SBOX_TLB_FLUSH, 1);
	
	/* DMA init */
	__mic_reset_dma_registers(kdd);

	return 0;
}
/** \brief Sets the size of the "OS reserved area" */
static void __mic_set_os_reserved_area(struct mic_device_data *kdd,
                                       unsigned long size)
{
	mic_write_sbox(kdd, SBOX_SCRATCH3, (unsigned int)size);
}

/** \brief Sets the size of the kernel image (maybe optional) */
static void __mic_set_os_size(struct mic_device_data *kdd, unsigned long size)
{
	mic_write_sbox(kdd, SBOX_SCRATCH5, (unsigned int)size);
}

/** \brief Load a kernel image from a file directly
 *
 * @param kdd A Knights Ferry device
 * @param filename A name of the kernel image file to load.
 */
int __mic_load_os_file(struct mic_device_data *kdd, const char *filename)
{
	struct file *file;
	loff_t size, pos = 0;
	long r;
	int ret = 0;
	mm_segment_t fs;

	file = filp_open(filename, O_RDONLY, 0);
	if (IS_ERR(file)) {
		return -ENOENT;
	}

	dprintf("mic_load_os_file: File %s opened.\n", filename);
	
	size = i_size_read(file->f_path.dentry->d_inode);
	if (size <= 0 || size > kdd->aperture_len) {
		dprintf("mic_load_os_file: File size %lld invalid.\n",
		        size);
		ret = -EINVAL;
		goto FIN;
	}
	dprintf("File size : %lld\n", size);

	fs = get_fs();
	set_fs(get_ds());

#ifdef CONFIG_MIC
	r = vfs_read(file, kdd->aperture_va, size, &pos);
#else
	r = vfs_read(file, kdd->aperture_va + kdd->os_load_offset, size, &pos);
#endif
	if (r != (long)size) {
		dprintf("mic_load_os_file: vfs_read(%lld) failed : %ld\n",
		        size, r);
		ret = -EINVAL;
	}

	set_fs(fs);

	/*
	 * Boot protocols
	 *   TODO: How to calculate size if "load to memory" is used??
	 *         (it seems that we can ignore this)
	 *         Is this reserve size adequate?
	 */
	__mic_set_os_reserved_area(kdd, PAGE_SIZE);
	__mic_set_os_size(kdd, size);

FIN:
	fput(file);

	return ret;
}

/** \brief Issue an interrupt to a core in a Knights Ferry device.
 *
 * @param kdd A Knights Ferry device
 * @param apicid The APIC ID of a CPU core to trigger an interrupt in.
 * @param vector The vector number of an interrupt to issue
 */
int mic_issue_interrupt(struct mic_device_data *kdd, int apicid, int vector)
{
	unsigned int val;

#ifdef CONFIG_MIC
	val = (vector << MIC_ICR_INTVEC_SHIFT) | 0;
#else
	val = (vector << MIC_ICR_INTVEC_SHIFT) | (1 << 13);
#endif
	
	mic_write_sbox(kdd, SBOX_APICICR0 + 4, apicid);
	mic_write_sbox(kdd, SBOX_APICICR0 + 0, val);

	return 0;
}

/** \brief Boot a kernel in a Knights Ferry device.
 *
 * @param kdd A Knights Ferry device
 * @param param The boot parameter for the new kernel.
 */
int mic_boot_os(struct mic_device_data *kdd, struct mic_boot_param *param)
{
	unsigned int address_high, address_low;
	unsigned long param_pa;

	param_pa = virt_to_phys(param);

	address_high = (unsigned int)(((unsigned long)param_pa) >> 32L);
	address_low = (unsigned int)(((unsigned long)param_pa) & 0xffffffff);

	/* Set boot parameter */
	mic_write_sbox(kdd, SBOX_SCRATCH12, 0);
	mic_write_sbox(kdd, SBOX_SCRATCH14, address_high);
	mic_write_sbox(kdd, SBOX_SCRATCH15, address_low);

	mic_issue_interrupt(kdd, kdd->bsp_apic_id, MIC_DMA_INTERRUPT_VECTOR);
	mic_enable_interrupts(kdd, MIC_DBR_ALL_MASK, MIC_DMA_ALL_MASK);

	return 0;
}

long __mic_debug_request(struct mic_device_data *kdd, 
                             int r, unsigned long arg)
{
	unsigned int u;

	switch (r) {

	case MIC_DEBUG_READ_SCRATCH:
		if (arg >= 0 && arg < 16) {
			u = mic_read_sbox(kdd, SBOX_SCRATCH0 + arg * 4);
			return (unsigned long)u;
		}
		break;

	case MIC_DEBUG_READ_SBOX:
		if (arg >= 0 && arg < SBOX_TLB_DATIN0) {
			u = mic_read_sbox(kdd, arg);
			
			return (unsigned long)u;
		}
		break;

	case MIC_DEBUG_DMA_TEST:
		return __mic_dma_test(kdd, arg);

	}
	return -EINVAL;
}

/** \brief Enable interrupts from a Knights Ferry device
 *
 * @param kdd       A Knights Ferry device
 * @param intr_mask Mask of interrupts to enable
 * @param dma_mask  Mask of DMA interrupts to enable
 */
static void mic_enable_interrupts(struct mic_device_data *kdd,
                                  int intr_mask, int dma_mask)
{
	unsigned int reg;

	reg = mic_read_sbox(kdd, SBOX_SICE0);
	reg |= SBOX_SICE0_DBR_BITS(intr_mask) | SBOX_SICE0_DMA_BITS(dma_mask);
	mic_write_sbox(kdd, SBOX_SICE0, reg);
}

/** \brief Disable interrupts from a Knights Ferry device
 *
 * Currently, all the interrupts are disabled (the two masks are ignored).
 * @param kdd       A Knights Ferry device
 * @param intr_mask Mask of interrupts to enable
 * @param dma_mask  Mask of DMA interrupts to enable
 */
static void mic_disable_interrupts(struct mic_device_data *kdd,
                                   int intr_mask, int dma_mask)
{
	unsigned int reg;
	/* TODO: masking */
	reg = mic_read_sbox(kdd, SBOX_SICE0);
	mic_write_sbox(kdd, SBOX_SICC0, reg);
}

/** \brief List of handlers that handle interrupts 
 * from the Knights Ferry device */
static LIST_HEAD(mic_interrupt_handlers);

/** \brief Add a handler for interrupts from the Knights Ferry device
 *
 * @param kdd A Knights Ferry device
 * @param itype Type of interrupts that the handler handles (ignored)
 * @param os IHK OS instance
 * @param os_priv The private structure related to the IHK OS instance.
 * @param h The descriptor of the handler to register
 */
int mic_add_interrupt_handler(struct mic_device_data *kdd, int itype,
                              ihk_os_t os, void *os_priv,
                              struct ihk_host_interrupt_handler *h)
{
	h->os = os;
	h->os_priv = os_priv;
	list_add_tail(&h->list, &mic_interrupt_handlers);

	return 0;
}

/** \brief Remove a handler for interrupts from the Knights Ferry device
 *
 * @param h The descriptor of the handler to unregister
 */
void mic_del_interrupt_handler(struct ihk_host_interrupt_handler *h)
{
	list_del(&h->list);
}

/** \brief IRQ Handler of Knights Ferry */
irqreturn_t mic_irq_handler(int irq, void *data)
{
	struct mic_device_data *kdd = data;
	unsigned int reg;
	struct ihk_host_interrupt_handler *h;

	/* ack */
	reg = mic_read_sbox(kdd, SBOX_SICR0);
	mic_write_sbox(kdd, SBOX_SICR0, reg);

#ifndef CONFIG_MIC	
	mic_enable_interrupts(kdd, MIC_DBR_ALL_MASK, MIC_DMA_ALL_MASK);
#endif

	/* XXX: Linear search? */
	list_for_each_entry(h, &mic_interrupt_handlers, list) {
		if (h->func) {
			h->func(h->os, h->os_priv, h->priv);
		}
	}

	return IRQ_HANDLED;
}

/** \brief Get a status of the kernel in a Knights Ferry device.
 *
 * Since there is only one kernel in a device, the parameter includes
 * no OS parameter.
 * @param kdd   A Knights Ferry device
 */
int __mic_os_get_status(struct mic_device_data *kdd)
{
	unsigned int v;

	v = mic_read_sbox(kdd, SBOX_SCRATCH12);
	if (v == 0) {
		return 0;
	} else if (v == 0x25470290) {
		return 1;
	} else if (v == 0x25470293) {
		return 2;
	} else {
		return -1;
	}
}

/** \brief Get the number of free pages on MIC.
 *
 * Since there is only one kernel in a device, the parameter includes
 * no OS parameter.
 * @param kdd   A Knights Ferry device
 */
int __mic_os_get_free_mem(struct mic_device_data *kdd)
{
	int timeout = 500;
	int ret;

	mic_write_sbox(kdd, SBOX_SCRATCH0, 0);
	mic_write_sbox(kdd, SBOX_SCRATCH1, 0);

	mic_issue_interrupt(kdd, 0, 0xd2);

	while ((ret = mic_read_sbox(kdd, SBOX_SCRATCH1)) == 0) {
		mdelay(100);
		timeout--;
	}
	
	if (timeout == 0) {
		printk("__mic_os_get_free_mem(): ERROR: timeout\n");
		return -1;
	}
	
	ret = mic_read_sbox(kdd, SBOX_SCRATCH0);
	
	return ret;
}

/** \brief Get various special memory areas of a Knights Ferry device.
 *
 * @param [in] kdd   A Knights Ferry device
 * @param [in] type  Type of the address area to query
 * @param [out] addr Starting address of the area (result)
 * @param [out] size Size of the memory area
 */
int __mic_get_special_addr(struct mic_device_data *kdd, 
                           enum ihk_special_addr_type type,
                           unsigned long *addr,
                           unsigned long *size)
{
	switch (type) {
	case IHK_SPADDR_KMSG:
		*addr = mic_read_sbox(kdd, SBOX_SCRATCH14);
		*size = mic_read_sbox(kdd, SBOX_SCRATCH11); // IHK_KMSG_SIZE

		if (*addr < PAGE_SIZE) { /* null or almost null pointer */
			return -EIO;
		}
		return 0;

	case IHK_SPADDR_MIKC_QUEUE_RECV:
		*addr = mic_read_sbox(kdd, SBOX_SCRATCH13);
		*size = MASTER_IKCQ_SIZE;
		if (*addr < PAGE_SIZE) {
			return -EIO;
		}
		return 0;

	case IHK_SPADDR_MIKC_QUEUE_SEND:
		*addr = mic_read_sbox(kdd, SBOX_SCRATCH15);
		*size = MASTER_IKCQ_SIZE;
		if (*addr < PAGE_SIZE) {
			return -EIO;
		}
		return 0;
	}
	return -EINVAL;
}
