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

#include "knf.h"
#include "knf_user.h"
#include "mic/micconst.h"
#include "mic/micsboxdefine.h"

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

int knf_device_init(struct pci_dev *dev, struct knf_device_data *kdd)
{
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

	knf_enable_interrupts(kdd, MIC_DBR_ALL_MASK, MIC_DMA_ALL_MASK);

	return 0;

FIN:
	pci_disable_device(dev);
	return err;
}

void knf_device_destroy(struct pci_dev *dev, struct knf_device_data *kdd)
{
	dprint_func_enter;
	dprint_var_p(dev);
	dprint_var_p(kdd);

	knf_disable_interrupts(kdd, MIC_DBR_ALL_MASK, MIC_DMA_ALL_MASK);
	if (kdd->irq) {
		free_irq(kdd->irq, kdd);
	}

	if (kdd->aperture_va)
		iounmap(kdd->aperture_va);
	if (kdd->mmio_va)
		iounmap(kdd->mmio_va);

	release_mem_region(kdd->aperture_pa, kdd->aperture_len);
	release_mem_region(kdd->mmio_pa, kdd->mmio_len);
	pci_disable_device(dev);
}

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

static unsigned int get_gtt_entry(struct knf_device_data *kdd, int entry)
{
	return readl((unsigned int *)((char *)(kdd->mmio_va) + 
	                              MMIO_GTT_BASE_OFFSET + 4 * entry));
}

static void set_gtt_entry(struct knf_device_data *kdd, int entry,
                          unsigned long phys, unsigned int enable)
{
	writel((unsigned int)((phys >> PAGE_SHIFT) << 1) | enable,
	       (unsigned int *)((char *)(kdd->mmio_va) + 
	                        MMIO_GTT_BASE_OFFSET + 4 * entry));
}

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

void knf_shutdown(struct knf_device_data *kdd)
{
	unsigned int reset;

	dprint_func_enter;

	knf_write_sbox(kdd, SBOX_SCRATCH2, 0);	
	knf_write_sbox(kdd, SBOX_SCRATCH12, 0);
	knf_write_sbox(kdd, SBOX_SCRATCH13, 0);
	knf_write_sbox(kdd, SBOX_SCRATCH14, 0);

	reset = knf_read_sbox(kdd, SBOX_RGCR);
	reset = 1;
	knf_write_sbox(kdd, SBOX_RGCR, reset);

	msleep(1000);

	__knf_prepare_os_load(kdd);
}

static void load_scratch_values(struct knf_device_data *kdd)
{
	unsigned long scratch2;

	scratch2 = knf_read_sbox(kdd, SBOX_SCRATCH2);

	kdd->os_load_offset = SCRATCH2_DOWNLOAD_ADDR(scratch2);
	kdd->bsp_apic_id = SCRATCH2_APIC_ID(scratch2);

	dprint_var_x8(kdd->os_load_offset);
	dprint_var_i4(kdd->bsp_apic_id);
}

#define WFBR_TIMEOUT 50

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
	
	return 0;
}
static void __knf_set_os_reserved_area(struct knf_device_data *kdd,
                                       unsigned long size)
{
	knf_write_sbox(kdd, SBOX_SCRATCH3, (unsigned int)size);
}

static void __knf_set_os_size(struct knf_device_data *kdd, unsigned long size)
{
	knf_write_sbox(kdd, SBOX_SCRATCH5, (unsigned int)size);
}

int __knf_load_os_file(struct knf_device_data *kdd, const char *filename)
{
	struct file *file;
	loff_t size, pos = 0;
	long r;
	int ret = 0;
	mm_segment_t fs;

	file = filp_open(filename, O_RDONLY, 0);
	if (!file) {
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

int knf_issue_interrupt(struct knf_device_data *kdd, int apicid, int vector)
{
	unsigned int val;

	val = (vector << MIC_ICR_INTVEC_SHIFT) | 0;
	
	knf_write_sbox(kdd, SBOX_APICICR0 + 4, apicid);
	knf_write_sbox(kdd, SBOX_APICICR0 + 0, val);

	return 0;
}

int knf_boot_os(struct knf_device_data *kdd)
{
	/* Make the state as zero */
	knf_write_sbox(kdd, SBOX_SCRATCH12, 0);

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

	}
	return -EINVAL;
}

static void knf_enable_interrupts(struct knf_device_data *kdd,
                                  int intr_mask, int dma_mask)
{
	unsigned int reg;

	reg = knf_read_sbox(kdd, SBOX_SICE0);
	reg |= SBOX_SICE0_DBR_BITS(intr_mask) | SBOX_SICE0_DMA_BITS(dma_mask);
	knf_write_sbox(kdd, SBOX_SICE0, reg);
}

static void knf_disable_interrupts(struct knf_device_data *kdd,
                                   int intr_mask, int dma_mask)
{
	unsigned int reg;
	/* TODO: masking */
	reg = knf_read_sbox(kdd, SBOX_SICE0);
	knf_write_sbox(kdd, SBOX_SICC0, reg);
}

static LIST_HEAD(knf_interrupt_handlers);

int knf_add_interrupt_handler(struct knf_device_data *kdd, int itype,
                              aal_os_t os, void *os_priv,
                              struct aal_host_interrupt_handler *h)
{
	h->os = os;
	h->os_priv = os_priv;
	list_add_tail(&h->list, &knf_interrupt_handlers);

	return 0;
}

void knf_del_interrupt_handler(struct aal_host_interrupt_handler *h)
{
	list_del(&h->list);
}

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
int __knf_get_special_addr(struct knf_device_data *kdd, 
                           enum aal_special_addr_type type,
                           unsigned long *addr,
                           unsigned long *size)
{
	switch (type) {
	case AAL_SPADDR_KMSG:
		*addr = knf_read_sbox(kdd, SBOX_SCRATCH14);
		*size = 8192; /* XXX: Magic Number */

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
