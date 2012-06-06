/**
 * \file knf_device.c
 * \brief AAL KNF Driver: KNF Driver Core
 *
 * This file treats the actual procedures for Knights Ferry boards.
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
#include <aal/misc/debug.h>
#include <ikc/msg.h>

#include "knf_user.h"
#include "mic.h"

void knf_device_destroy(struct pci_dev *dev, struct knf_device_data *data);

static void load_scratch_values(struct knf_device_data *kdd);
void knf_shutdown(struct knf_device_data *kdd);
static irqreturn_t knf_irq_handler(int irq, void *data);

static void knf_enable_interrupts(struct knf_device_data *kdd,
                                  int intr_mask, int dma_mask);
static void knf_disable_interrupts(struct knf_device_data *kdd,
                                   int intr_mask, int dma_mask);
static void knf_write_sbox(struct knf_device_data *kdd, int offset,
                           unsigned int value);

void __knf_dma_init(struct knf_device_data *kdd);
void __knf_dma_finalize(struct knf_device_data *kdd);
int __knf_dma_test(struct knf_device_data *kdd, unsigned long arg);
void __knf_reset_dma_registers(struct knf_device_data *kdd);

/**
 * \brief Initialize the specified Knights Ferry board.
 *
 * \param dev  A pci_dev structure corresponding to the device to initialize
 * \param kdd An internal data structure in this driver.
 */
int knf_device_init(struct pci_dev *dev, struct knf_device_data *kdd)
{
	int i, err = 0;

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
	
	if (!request_mem_region(kdd->aperture_pa, kdd->aperture_len, "knf")) {
		err = -ENOMEM;
		goto FIN;
	}

	if (!request_mem_region(kdd->mmio_pa, kdd->mmio_len, "knf")) {
		release_mem_region(kdd->aperture_pa, kdd->aperture_len);
		err = -ENOMEM;
		goto FIN;
	}

	if (!(kdd->mmio_va = ioremap_nocache(kdd->mmio_pa, kdd->mmio_len))) {
		knf_device_destroy(dev, kdd);
		return -ENOMEM;
	}	
	                                   
	if (!(kdd->aperture_va = ioremap_wc(kdd->aperture_pa,
	                                    kdd->aperture_len))) {
		knf_device_destroy(dev, kdd);
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

	if (request_irq(dev->irq, knf_irq_handler, IRQF_SHARED, "knf", kdd)
	    != 0) {
		dprintf("Failed to request irq");
		knf_device_destroy(dev, kdd);
		return -ENOMEM;
	}
	kdd->irq = dev->irq;

	__knf_dma_init(kdd);

	/* XXX: This should be dynamically obtained from the card */
	kdd->mem_region.start = 0;
	kdd->mem_region.size = 0x80000000;
	kdd->mem_info.n_mappable = kdd->mem_info.n_available = 1;
	kdd->mem_info.n_fixed = 0;
	kdd->mem_info.available = kdd->mem_info.mappable = &kdd->mem_region;
	kdd->mem_info.fixed = NULL;

	/* TODO: do this in a more generic way! */
	for (i = 0; i < 128; i++) {
		kdd->cpu_hw_ids[i] = i;
	}

	/* Currently BSP id + 4 is number of cores */
	kdd->cpu_info.n_cpus = kdd->bsp_apic_id + 4; 
	kdd->cpu_info.hw_ids = kdd->cpu_hw_ids;

	knf_enable_interrupts(kdd, MIC_DBR_ALL_MASK, MIC_DMA_ALL_MASK);

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
void knf_device_destroy(struct pci_dev *dev, struct knf_device_data *kdd)
{
	dprint_func_enter;
	dprint_var_p(dev);
	dprint_var_p(kdd);

	knf_disable_interrupts(kdd, MIC_DBR_ALL_MASK, MIC_DMA_ALL_MASK);
	if (kdd->irq) {
		free_irq(kdd->irq, kdd);
	}

	__knf_dma_finalize(kdd);

	if (kdd->aperture_va)
		iounmap(kdd->aperture_va);
	if (kdd->mmio_va)
		iounmap(kdd->mmio_va);

	release_mem_region(kdd->aperture_pa, kdd->aperture_len);
	release_mem_region(kdd->mmio_pa, kdd->mmio_len);
	pci_disable_device(dev);
}

/** \brief Get an entry in GTT.
 *
 * @param kdd  An interal structure for a Knights Ferry device
 * @param entry Index in the GTT to query (0 to GTT_SIZE / 4)
 */
static unsigned int get_gtt_entry(struct knf_device_data *kdd, int entry)
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
static void set_gtt_entry(struct knf_device_data *kdd, int entry,
                          unsigned long phys, unsigned int enable)
{
	writel((unsigned int)((phys >> PAGE_SHIFT) << 1) | enable,
	       (unsigned int *)((char *)(kdd->mmio_va) + 
	                        MMIO_GTT_BASE_OFFSET + 4 * entry));
}

/** \brief Maps an memory area to the aperture in a Knights Ferry device.
 *
 * @param kdd        A Knights Ferry device
 * @param ap_address Start address in the aperture to be mapped to.
 * @param phys       Start physical address in Knights Ferry to be mapped.
 * @param napges     Number of pages to map
 */
int knf_map_aperture(struct knf_device_data *kdd, unsigned long ap_address,
                     unsigned long phys, int npages)
{
	int i = (int)((ap_address - kdd->aperture_pa) >> PAGE_SHIFT);
	int e = i + npages;
	unsigned long flags;

	dprintf("knf: map_aperture %lx - %lx => %lx\n", ap_address,
	        ap_address + (npages << PAGE_SHIFT), phys);

	spin_lock_irqsave(&kdd->lock, flags);
	for (; i < e; i++) {
		set_gtt_entry(kdd, i, phys, 1);
		phys += PAGE_SIZE;
	}
	spin_unlock_irqrestore(&kdd->lock, flags);

	smp_mb();

	knf_write_sbox(kdd, SBOX_SBQ_FLUSH, 1);
	knf_write_sbox(kdd, SBOX_TLB_FLUSH, 1);

	return 0;
}

/** \brief Unmaps an memory area to the aperture in a Knights Ferry device.
 *
 * @param kdd        A Knights Ferry device
 * @param ap_address Start address in the aperture to unmap
 * @param napges     Number of pages to unmap
 */
int knf_unmap_aperture(struct knf_device_data *kdd, unsigned long ap_address,
                       int npages)
{
	int i = (int)((ap_address - kdd->aperture_pa) >> PAGE_SHIFT);
	int e = i + npages;
	unsigned long flags;

	dprintf("knf: unmap_aperture %lx - %lx\n", ap_address,
	        ap_address + (npages << PAGE_SHIFT));

	spin_lock_irqsave(&kdd->lock, flags);
	for (; i < e; i++) {
		set_gtt_entry(kdd, i, 0, 0);
	}
	spin_unlock_irqrestore(&kdd->lock, flags);

	return 0;
}

int __knf_prepare_os_load(struct knf_device_data *kdd);

/** \brief Shuts down a Knights Ferry device */
void knf_shutdown(struct knf_device_data *kdd)
{
	unsigned int reset;

	dprint_func_enter;

	knf_write_sbox(kdd, SBOX_SCRATCH2, 0);	
	knf_write_sbox(kdd, SBOX_SCRATCH12, 0);
	knf_write_sbox(kdd, SBOX_SCRATCH13, 0);
	knf_write_sbox(kdd, SBOX_SCRATCH14, 0);
	knf_write_sbox(kdd, SBOX_SCRATCH15, 0);

	reset = knf_read_sbox(kdd, SBOX_RGCR);
	reset = 1;
	knf_write_sbox(kdd, SBOX_RGCR, reset);

	msleep(1000);

	__knf_prepare_os_load(kdd);
}

/** \brief Sets up scratch values in Knights Ferry for booting */
static void load_scratch_values(struct knf_device_data *kdd)
{
	unsigned long scratch2;

	scratch2 = knf_read_sbox(kdd, SBOX_SCRATCH2);

	kdd->os_load_offset = SCRATCH2_DOWNLOAD_ADDR(scratch2);
	kdd->bsp_apic_id = SCRATCH2_APIC_ID(scratch2);

	dprint_var_x8(kdd->os_load_offset);
	dprint_var_i4(kdd->bsp_apic_id);
}

/** \brief Timeout for __wait_for_bootstrap_ready in 100-ms unit */
#define WFBR_TIMEOUT 50

/** \brief Waits for the Knights Ferry device to get ready by spinning */
static int __wait_for_bootstrap_ready(struct knf_device_data *kdd)
{
	unsigned int scratch2;
	int count;

	for (count = 0; count < WFBR_TIMEOUT; count++) {
		scratch2 = knf_read_sbox(kdd, SBOX_SCRATCH2);
		
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
int __knf_prepare_os_load(struct knf_device_data *kdd)
{
	unsigned long i, phys;

	if (__wait_for_bootstrap_ready(kdd) < 0) {
		return -EBUSY;
	}

	dprint_var_x8(kdd->os_load_offset);
	phys = kdd->os_load_offset;

	for (i = 0; i < (kdd->aperture_len >> PAGE_SHIFT); i++) {
		set_gtt_entry(kdd, i, phys, 1);

		phys += PAGE_SIZE;
	}

	smp_mb();

	knf_write_sbox(kdd, SBOX_TLB_FLUSH, 1);
	
	/* DMA init */
	__knf_reset_dma_registers(kdd);

	return 0;
}
/** \brief Sets the size of the "OS reserved area" */
static void __knf_set_os_reserved_area(struct knf_device_data *kdd,
                                       unsigned long size)
{
	knf_write_sbox(kdd, SBOX_SCRATCH3, (unsigned int)size);
}

/** \brief Sets the size of the kernel image (maybe optional) */
static void __knf_set_os_size(struct knf_device_data *kdd, unsigned long size)
{
	knf_write_sbox(kdd, SBOX_SCRATCH5, (unsigned int)size);
}

/** \brief Load a kernel image from a file directly
 *
 * @param kdd A Knights Ferry device
 * @param filename A name of the kernel image file to load.
 */
int __knf_load_os_file(struct knf_device_data *kdd, const char *filename)
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

	dprintf("knf_load_os_file: File %s opened.\n", filename);
	
	size = i_size_read(file->f_path.dentry->d_inode);
	if (size <= 0 || size > kdd->aperture_len) {
		dprintf("knf_load_os_file: File size %lld invalid.\n",
		        size);
		ret = -EINVAL;
		goto FIN;
	}
	dprintf("File size : %lld\n", size);

	fs = get_fs();
	set_fs(get_ds());

	r = vfs_read(file, kdd->aperture_va, size, &pos);
	if (r != (long)size) {
		dprintf("knf_load_os_file: vfs_read(%lld) failed : %ld\n",
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
	__knf_set_os_reserved_area(kdd, PAGE_SIZE);
	__knf_set_os_size(kdd, size);

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
int knf_issue_interrupt(struct knf_device_data *kdd, int apicid, int vector)
{
	unsigned int val;

	val = (vector << MIC_ICR_INTVEC_SHIFT) | 0;
	
	knf_write_sbox(kdd, SBOX_APICICR0 + 4, apicid);
	knf_write_sbox(kdd, SBOX_APICICR0 + 0, val);

	return 0;
}

/** \brief Boot a kernel in a Knights Ferry device.
 *
 * @param kdd A Knights Ferry device
 * @param param The boot parameter for the new kernel.
 */
int knf_boot_os(struct knf_device_data *kdd, struct knf_boot_param *param)
{
	unsigned int address_high, address_low;
	unsigned long param_pa;

	param_pa = virt_to_phys(param);

	address_high = (unsigned int)(((unsigned long)param_pa) >> 32L);
	address_low = (unsigned int)(((unsigned long)param_pa) & 0xffffffff);

	/* Set boot parameter */
	knf_write_sbox(kdd, SBOX_SCRATCH12, 0);
	knf_write_sbox(kdd, SBOX_SCRATCH14, address_high);
	knf_write_sbox(kdd, SBOX_SCRATCH15, address_low);

	knf_issue_interrupt(kdd, kdd->bsp_apic_id, MIC_DMA_INTERRUPT_VECTOR);
	knf_enable_interrupts(kdd, MIC_DBR_ALL_MASK, MIC_DMA_ALL_MASK);

	return 0;
}

long __knf_debug_request(struct knf_device_data *kdd, 
                             int r, unsigned long arg)
{
	unsigned int u;

	switch (r) {

	case KNF_DEBUG_READ_SCRATCH:
		if (arg >= 0 && arg < 16) {
			u = knf_read_sbox(kdd, SBOX_SCRATCH0 + arg * 4);
			return (unsigned long)u;
		}
		break;

	case KNF_DEBUG_READ_SBOX:
		if (arg >= 0 && arg < SBOX_TLB_DATIN0) {
			u = knf_read_sbox(kdd, arg);
			
			return (unsigned long)u;
		}
		break;

	case KNF_DEBUG_DMA_TEST:
		return __knf_dma_test(kdd, arg);

	}
	return -EINVAL;
}

/** \brief Enable interrupts from a Knights Ferry device
 *
 * @param kdd       A Knights Ferry device
 * @param intr_mask Mask of interrupts to enable
 * @param dma_mask  Mask of DMA interrupts to enable
 */
static void knf_enable_interrupts(struct knf_device_data *kdd,
                                  int intr_mask, int dma_mask)
{
	unsigned int reg;

	reg = knf_read_sbox(kdd, SBOX_SICE0);
	reg |= SBOX_SICE0_DBR_BITS(intr_mask) | SBOX_SICE0_DMA_BITS(dma_mask);
	knf_write_sbox(kdd, SBOX_SICE0, reg);
}

/** \brief Disable interrupts from a Knights Ferry device
 *
 * Currently, all the interrupts are disabled (the two masks are ignored).
 * @param kdd       A Knights Ferry device
 * @param intr_mask Mask of interrupts to enable
 * @param dma_mask  Mask of DMA interrupts to enable
 */
static void knf_disable_interrupts(struct knf_device_data *kdd,
                                   int intr_mask, int dma_mask)
{
	unsigned int reg;
	/* TODO: masking */
	reg = knf_read_sbox(kdd, SBOX_SICE0);
	knf_write_sbox(kdd, SBOX_SICC0, reg);
}

/** \brief List of handlers that handle interrupts 
 * from the Knights Ferry device */
static LIST_HEAD(knf_interrupt_handlers);

/** \brief Add a handler for interrupts from the Knights Ferry device
 *
 * @param kdd A Knights Ferry device
 * @param itype Type of interrupts that the handler handles (ignored)
 * @param os AAL OS instance
 * @param os_priv The private structure related to the AAL OS instance.
 * @param h The descriptor of the handler to register
 */
int knf_add_interrupt_handler(struct knf_device_data *kdd, int itype,
                              aal_os_t os, void *os_priv,
                              struct aal_host_interrupt_handler *h)
{
	h->os = os;
	h->os_priv = os_priv;
	list_add_tail(&h->list, &knf_interrupt_handlers);

	return 0;
}

/** \brief Remove a handler for interrupts from the Knights Ferry device
 *
 * @param h The descriptor of the handler to unregister
 */
void knf_del_interrupt_handler(struct aal_host_interrupt_handler *h)
{
	list_del(&h->list);
}

/** \brief IRQ Handler of Knights Ferry */
irqreturn_t knf_irq_handler(int irq, void *data)
{
	struct knf_device_data *kdd = data;
	unsigned int reg;
	struct aal_host_interrupt_handler *h;

	/* ack */
	reg = knf_read_sbox(kdd, SBOX_SICR0);
	knf_write_sbox(kdd, SBOX_SICR0, reg);

	/* XXX: Linear search? */
	list_for_each_entry(h, &knf_interrupt_handlers, list) {
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
int __knf_os_get_status(struct knf_device_data *kdd)
{
	unsigned int v;

	v = knf_read_sbox(kdd, SBOX_SCRATCH12);
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
/** \brief Get various special memory areas of a Knights Ferry device.
 *
 * @param [in] kdd   A Knights Ferry device
 * @param [in] type  Type of the address area to query
 * @param [out] addr Starting address of the area (result)
 * @param [out] size Size of the memory area
 */
int __knf_get_special_addr(struct knf_device_data *kdd, 
                           enum aal_special_addr_type type,
                           unsigned long *addr,
                           unsigned long *size)
{
	switch (type) {
	case AAL_SPADDR_KMSG:
		*addr = knf_read_sbox(kdd, SBOX_SCRATCH14);
		*size = knf_read_sbox(kdd, SBOX_SCRATCH11); // AAL_KMSG_SIZE

		if (*addr < PAGE_SIZE) { /* null or almost null pointer */
			return -EIO;
		}
		return 0;

	case AAL_SPADDR_MIKC_QUEUE_RECV:
		*addr = knf_read_sbox(kdd, SBOX_SCRATCH13);
		*size = MASTER_IKCQ_SIZE;
		if (*addr < PAGE_SIZE) {
			return -EIO;
		}
		return 0;

	case AAL_SPADDR_MIKC_QUEUE_SEND:
		*addr = knf_read_sbox(kdd, SBOX_SCRATCH15);
		*size = MASTER_IKCQ_SIZE;
		if (*addr < PAGE_SIZE) {
			return -EIO;
		}
		return 0;
	}
	return -EINVAL;
}
