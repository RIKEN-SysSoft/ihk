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
	                                   
	if (!(kdd->aperture_va = ioremap_nocache(kdd->aperture_pa,
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

static void set_gtt_entry(struct knf_device_data *kdd, int entry,
                          unsigned long phys)
{
	writel((unsigned int)((phys >> PAGE_SHIFT) << 1) | 1,
	       (unsigned int *)((char *)(kdd->mmio_va) + 
	                        MMIO_GTT_BASE_OFFSET + 4 * entry));
}

void knf_shutdown(struct knf_device_data *kdd)
{
	unsigned int reset;

	knf_write_sbox(kdd, SBOX_SCRATCH2, 0);	
	knf_write_sbox(kdd, SBOX_SCRATCH14, 0);

	reset = knf_read_sbox(kdd, SBOX_RGCR);
	reset = 1;
	knf_write_sbox(kdd, SBOX_RGCR, reset);

	msleep(1000);
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
		set_gtt_entry(kdd, i, phys);

		phys += PAGE_SIZE;
	}

	smp_mb();

	knf_write_sbox(kdd, SBOX_TLB_FLUSH, 1);
	
	return 0;
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
FIN:
	fput(file);

	return ret;
}

static int knf_issue_interrupt(struct knf_device_data *kdd,
                               int apicid, int vector)
{
	unsigned int val;

	val = (vector << MIC_ICR_INTVEC_SHIFT) | 0;
	
	knf_write_sbox(kdd, SBOX_APICICR0 + 4, apicid);
	knf_write_sbox(kdd, SBOX_APICICR0 + 0, val);

	return 0;
}

int knf_boot_os(struct knf_device_data *kdd)
{
	knf_issue_interrupt(kdd, kdd->bsp_apic_id, MIC_DMA_INTERRUPT_VECTOR);
	knf_enable_interrupts(kdd, MIC_DBR_ALL_MASK, MIC_DMA_ALL_MASK);

	return 0;
}

long __knf_debug_request(struct knf_device_data *kdd, 
                             int r, unsigned long arg)
{
	switch (r) {

	case KNF_DEBUG_READ_SCRATCH:
		if (arg >= 0 && arg < 16) {
			unsigned int u;

			u = knf_read_sbox(kdd, SBOX_SCRATCH0 + arg * 4);
			return (long)u;
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


irqreturn_t knf_irq_handler(int irq, void *data)
{
	struct knf_device_data *kdd = data;
	unsigned int reg;

	/* ack */
	reg = knf_read_sbox(kdd, SBOX_SICR0);
	knf_write_sbox(kdd, SBOX_SICR0, reg);

	dprintf("Interrupt from KNF!\n");

	return IRQ_HANDLED;
}
