/**
 * \file ihk_mic_driver.c
 *  License details are found in the file LICENSE.
 * \brief
 *	IHK MIC Driver: IHK Host Driver for Knights Ferry
 * \author Taku Shimosawa  <shimosawa@is.s.u-tokyo.ac.jp> \par
 * Copyright (C) 2011-2012 Taku Shimosawa <shimosawa@is.s.u-tokyo.ac.jp>
 */
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <ihk/ihk_host_driver.h>
#include <ihk/ihk_host_misc.h>
#include <ihk/ihk_host_user.h>
#include <ihk/misc/debug.h>
#include "mic.h"

/**
 * \var mic_pci_ids
 * \brief The PCI IDs of Knights Ferry boards
 */
static struct pci_device_id mic_pci_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2240), },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2241), },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2242), },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2243), },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2249), },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x224a), },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2250), },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x225c), },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x225d), },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2259), },
	{ 0, }
};

//#define DEBUG_PRINT_IHK_MIC

#ifdef DEBUG_PRINT_IHK_MIC
#define dprintk printk
#else
#define dprintk(...)
#endif

/**** OS Section ****/
extern int __mic_prepare_os_load(struct mic_device_data *kdd);
extern int __mic_load_os_file(struct mic_device_data *kdd, const char *fn);
extern int mic_boot_os(struct mic_device_data *kdd, struct mic_boot_param *);
extern void mic_shutdown(struct mic_device_data *kdd);
extern int __mic_os_get_status(struct mic_device_data *kdd);
extern int __mic_os_get_free_mem(struct mic_device_data *kdd);
extern int mic_issue_interrupt(struct mic_device_data *kdd,
                               int apicid, int vector);
extern int mic_map_aperture(struct mic_device_data *kdd,
                            unsigned long ap_address, unsigned long phys,
                            int npages);
extern int mic_unmap_aperture(struct mic_device_data *kdd, 
                              unsigned long ap_address, int npages);
extern int __mic_get_special_addr(struct mic_device_data *kdd, 
                                  enum ihk_special_addr_type type,
                                  unsigned long *addr, unsigned long *size);
extern int mic_add_interrupt_handler(struct mic_device_data *kdd, int itype,
                                     ihk_os_t os, void *os_priv,
                                     struct ihk_host_interrupt_handler *h);
extern void mic_del_interrupt_handler(struct ihk_host_interrupt_handler *h);

static int mic_ihk_os_boot(ihk_os_t ihk_os, void *priv, int flag)
{
	struct mic_os_data *os = priv;
	struct mic_device_data *kdd = os->dev;

	return mic_boot_os(kdd, &os->boot_param);
}

static int mic_ihk_os_load_file(ihk_os_t ihk_os, void *priv, const char *fn)
{
	struct mic_os_data *os = priv;
	struct mic_device_data *kdd = os->dev;

	return __mic_load_os_file(kdd, fn);
}

static int mic_ihk_os_load_mem(ihk_os_t ihk_os, void *priv, const char *buf,
                               unsigned long size, long offset)
{
	struct mic_os_data *os = priv;
	struct mic_device_data *kdd = os->dev;

	if (offset + size > kdd->aperture_len) {
		return -ENOMEM;
	}
	memcpy(kdd->aperture_va + offset, buf, size);

	return 0;
}

static int mic_ihk_os_shutdown(ihk_os_t ihk_os, void *priv, int flag)
{
	struct mic_os_data *os = priv;
	struct mic_device_data *kdd = os->dev;

	mic_shutdown(kdd);
	
	return 0;
}

/**
 * \brief Allocate resources in a Knights Ferry device for an OS
 *
 * XXX: Because we assume that only one kernel is running, all the resources
 * are dedicated to the only kernel.
 * Therefore, this function returns always 0 (successful)
 */
static int mic_ihk_os_alloc_resource(ihk_os_t ihk_os, void *priv,
                                     struct ihk_resource *resource)
{
/*	struct builtin_os_data *os = priv; */
	return 0;
}

static enum ihk_os_status mic_ihk_os_query_status(ihk_os_t ihk_os, void *priv)
{
	struct mic_os_data *os = priv;
	struct mic_device_data *kdd = os->dev;
	int v;

	/* XXX: Before booting, this should be maintained by this driver */
	v = __mic_os_get_status(kdd);
	if (v == 0) {
		return IHK_OS_STATUS_BOOTING;
	} else if (v == 1) {
		return IHK_OS_STATUS_BOOTED;
	} else if (v == 2) {
		return IHK_OS_STATUS_READY;
	}
	return IHK_OS_STATUS_NOT_BOOTED;
}

static int mic_ihk_os_query_free_mem(ihk_os_t ihk_os, void *priv)
{
	struct mic_os_data *os = priv;
	struct mic_device_data *kdd = os->dev;
	
	return __mic_os_get_free_mem(kdd);
}

static int mic_ihk_os_wait_for_status(ihk_os_t ihk_os, void *priv,
                                      enum ihk_os_status status, 
                                      int sleepable, int timeout)
{
	enum ihk_os_status s;
	if (sleepable) {
		/* TODO: Enable notification of status change, and wait */
		return -1;
	} else {
		/* Polling */
		while ((s = mic_ihk_os_query_status(ihk_os, priv)),
		       s != status && s < IHK_OS_STATUS_SHUTDOWN 
		       && timeout > 0) {
			mdelay(100);
			timeout--;
		}
		return s == status ? 0 : -1;
	}
}

static int mic_ihk_os_issue_interrupt(ihk_os_t ihk_os, void *priv,
                                      int cpu, int vector)
{
	struct mic_os_data *os = priv;
	struct mic_device_data *kdd = os->dev;

	/* cpu to apic id based on bsp */
	if (cpu == 0) {
		/* BSP processor is the first */
		cpu = kdd->bsp_apic_id;
	} else if (cpu <= kdd->bsp_apic_id) {
		cpu = cpu - 1;
	} else {
		cpu = cpu;
	}
	dprintk("mic_ihk_os_issue_interrupt, cpu: %d, vector: %d\n", cpu, vector);
	return mic_issue_interrupt(kdd, cpu, vector);
}

static unsigned long mic_ihk_os_map_memory(ihk_os_t ihk_os, void *priv,
                                           unsigned long remote_phys,
                                           unsigned long size)
{
	struct mic_os_data *os = priv;
	struct mic_device_data *kdd = os->dev;
#ifdef CONFIG_MIC	
	unsigned long phys;
#endif

	dprint_func_enter;
	dprint_var_x8(size);
#ifdef CONFIG_MIC	
	size >>= PAGE_SHIFT;

	if (!(phys = ihk_pagealloc_alloc(kdd->alloc_desc, size))) {
		return (unsigned long)-ENOMEM;
	}
	
	if (mic_map_aperture(kdd, phys, remote_phys, size)) {
		ihk_pagealloc_free(kdd->alloc_desc, phys, size);
		return (unsigned long)-ENOMEM;
	}

	return phys;
#endif
	return kdd->aperture_pa + remote_phys;
}

static int mic_ihk_os_unmap_memory(ihk_os_t ihk_os, void *priv,
                                   unsigned long phys, unsigned long size)
{
	struct mic_os_data *os = priv;
	struct mic_device_data *kdd = os->dev;

	size >>= PAGE_SHIFT;

	mic_unmap_aperture(kdd, phys, size);
	ihk_pagealloc_free(kdd->alloc_desc, phys, size);

	return 0;
}

/** \brief Register an interrupt handler for interrupts from Knights Ferry */
static int mic_ihk_os_reg_intr(ihk_os_t ihk_os, void *priv, int itype,
                               struct ihk_host_interrupt_handler *h)
{
	struct mic_os_data *os = priv;
	struct mic_device_data *kdd = os->dev;

	return mic_add_interrupt_handler(kdd, itype, ihk_os, priv, h);
}

static int mic_ihk_os_unreg_intr(ihk_os_t ihk_os, void *priv, int itype,
                                 struct ihk_host_interrupt_handler *h)
{
	mic_del_interrupt_handler(h);
	return 0;
}

static int mic_ihk_os_get_special_addr(ihk_os_t ihk_os, void *priv,
                                       enum ihk_special_addr_type type,
                                       unsigned long *addr,
                                       unsigned long *size)
{
	int v;
	struct mic_os_data *os = priv;
	struct mic_device_data *kdd = os->dev;

	v = __mic_os_get_status(kdd);
	if (v < 1) {
		return -EBUSY;
	}

	return __mic_get_special_addr(kdd, type, addr, size);
}

static long mic_ihk_os_debug_request(ihk_os_t ihk_os, void *priv,
                                     unsigned int req, unsigned long arg)
{
	switch (req) {
	case IHK_OS_DEBUG_START:
		mic_ihk_os_issue_interrupt(ihk_os, priv, 0, arg);
		return 0;
	}
	return -EINVAL;
}

static struct ihk_mem_info *mic_ihk_os_get_memory_info(ihk_os_t ihk_os,
                                                       void *priv)
{
	struct mic_os_data *os = priv;
	struct mic_device_data *kdd = os->dev;

	return &kdd->mem_info;
}

static struct ihk_cpu_info *mic_ihk_os_get_cpu_info(ihk_os_t ihk_os, void *priv)
{
	struct mic_os_data *os = priv;
	struct mic_device_data *kdd = os->dev;

	return &kdd->cpu_info;
}

static int mic_ihk_os_set_kargs(ihk_os_t ihk_os, void *priv, char *buf)
{
	struct mic_os_data *os = priv;

	strncpy(os->boot_param.kernel_args, buf,
	        sizeof(os->boot_param.kernel_args));

	return 0;
}

static struct ihk_os_ops mic_ihk_os_ops = {
	.load_mem = mic_ihk_os_load_mem,
	.load_file = mic_ihk_os_load_file,

	.boot = mic_ihk_os_boot,
	.shutdown = mic_ihk_os_shutdown,

	.alloc_resource = mic_ihk_os_alloc_resource,
	.query_status = mic_ihk_os_query_status,
	.query_free_mem = mic_ihk_os_query_free_mem,
	.wait_for_status = mic_ihk_os_wait_for_status,
	.issue_interrupt = mic_ihk_os_issue_interrupt,

	.register_handler = mic_ihk_os_reg_intr,
	.unregister_handler = mic_ihk_os_unreg_intr,

	.map_memory = mic_ihk_os_map_memory,
	.unmap_memory = mic_ihk_os_unmap_memory,

	.get_special_addr = mic_ihk_os_get_special_addr,
	.set_kargs = mic_ihk_os_set_kargs,

	.debug_request = mic_ihk_os_debug_request,

	.get_memory_info = mic_ihk_os_get_memory_info,
	.get_cpu_info = mic_ihk_os_get_cpu_info,
};	

static struct ihk_register_os_data mic_os_reg_data = {
	.name = "micos_base",
	.flag = 0,
	.ops = &mic_ihk_os_ops,
};

/**** Device Section ****/

extern int mic_device_init(struct pci_dev *dev, struct mic_device_data *data);
extern void mic_device_destroy(struct pci_dev *dev,
                               struct mic_device_data *data);
extern long __mic_debug_request(struct mic_device_data *kdd, 
                                int r, unsigned long arg);
ihk_dma_channel_t mic_ihk_get_dma_channel(ihk_device_t dev, void *priv,
                                          int channel);

static int mic_ihk_init(ihk_device_t ihk_dev, void *priv)
{
	struct mic_device_data *data = priv;

	return mic_device_init(data->dev, data);
}

static int mic_ihk_exit(ihk_device_t ihk_dev, void *priv)
{
	struct mic_device_data *data = priv;

	mic_device_destroy(data->dev, data);

	return 0;
}

static int mic_ihk_create_os(ihk_device_t ihk_dev, void *priv,
                             unsigned long arg, ihk_os_t ihk_os,
                             struct ihk_register_os_data *regdata)
{
	unsigned long flags;
	struct mic_device_data *data = priv;
	struct mic_os_data *os;

	/* Allocate a device. First kernel should be one */
	spin_lock_irqsave(&data->lock, flags);
	if (data->status != 0 && data->status != 2) {
		spin_unlock_irqrestore(&data->lock, flags);
		return -EBUSY;
	}
	data->status = 1;
	spin_unlock_irqrestore(&data->lock, flags);

	*regdata = mic_os_reg_data;
	os = kzalloc(sizeof(struct mic_os_data), GFP_KERNEL);
	if (!os) {
		data->status = 0; /* No other one should reach here */
		return -ENOMEM;
	}
	spin_lock_init(&os->lock);
	os->dev = data;

	if (__mic_prepare_os_load(data) != 0) {
		data->status = 0;
		return -EBUSY;
	}

	regdata->priv = os;

	return 0;
}

static long mic_ihk_debug_request(ihk_device_t ihk_dev, void *priv,
                                  unsigned int r, unsigned long arg)
{
	struct mic_device_data *kdd = priv;

	return __mic_debug_request(kdd, r, arg);
}

static unsigned long mic_ihk_map_memory(ihk_os_t ihk_os, void *priv,
                                        unsigned long remote_phys,
                                        unsigned long size)
{
	struct mic_device_data *kdd = priv;
#ifdef CONFIG_MIC	
	unsigned long phys;
#endif

	dprint_func_enter;
	dprint_var_x8(size);

#ifdef CONFIG_MIC
	size >>= PAGE_SHIFT;

	if (!(phys = ihk_pagealloc_alloc(kdd->alloc_desc, size))) {
		return (unsigned long)-ENOMEM;
	}
	
	if (mic_map_aperture(kdd, phys, remote_phys, size)) {
		ihk_pagealloc_free(kdd->alloc_desc, phys, size);
		return (unsigned long)-ENOMEM;
	}

	return phys;
#else
	return kdd->aperture_pa + remote_phys;
#endif
}

static int mic_ihk_unmap_memory(ihk_device_t ihk_dev, void *priv,
                                unsigned long phys, unsigned long size)
{
#ifdef CONFIG_MIC
	struct mic_device_data *kdd = priv;

	size >>= PAGE_SHIFT;

	mic_unmap_aperture(kdd, phys, size);
	ihk_pagealloc_free(kdd->alloc_desc, phys, size);
#endif

	return 0;
}

static void *mic_ihk_map_virtual(ihk_device_t ihk_dev, void *priv,
                                 unsigned long phys, unsigned long size,
                                 void *virt, int flags)
{
	struct mic_device_data *kdd = priv;
	
	if (!virt && phys >= kdd->aperture_pa
	    && phys + size < kdd->aperture_pa + kdd->aperture_len) {
		return ((char *)kdd->aperture_va) + (phys - kdd->aperture_pa);
	}
	return ihk_host_map_generic(ihk_dev, phys, virt, size, flags);
}

static int mic_ihk_unmap_virtual(ihk_device_t ihk_dev, void *priv,
                                  void *virt, unsigned long size)
{
	struct mic_device_data *kdd = priv;

	if (virt >= kdd->aperture_va
	    && (unsigned char *)virt < ((unsigned char *)kdd->aperture_va
	                                + kdd->aperture_len)) {
		return 0;
	}

	return ihk_host_unmap_generic(ihk_dev, virt, size);
}

static struct ihk_device_ops mic_ihk_device_ops = {
	.init = mic_ihk_init,
	.exit = mic_ihk_exit,
	.create_os = mic_ihk_create_os,
	.debug_request = mic_ihk_debug_request,
	.map_memory = mic_ihk_map_memory,
	.unmap_memory = mic_ihk_unmap_memory,
	.map_virtual = mic_ihk_map_virtual,
	.unmap_virtual = mic_ihk_unmap_virtual,
	.get_dma_channel = mic_ihk_get_dma_channel,
};	

static struct ihk_register_device_data mic_dev_reg_data = {
	.name = "mic",
	.flag = 0,
	.ops = &mic_ihk_device_ops,
};

/**
 * \func mic_probe
 * \brief Checks if the specified device can be handled by this driver.
 */
static int mic_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	struct mic_device_data *data;
	ihk_device_t ihkd;

	printk(KERN_INFO "mic: Found at %s. (vendor = %x, device = %x)\n",
	       pci_name(dev), id->vendor, id->device);

	data = kzalloc(sizeof(struct mic_device_data), GFP_KERNEL);
	if (!data) {
		return -ENOMEM;
	}
	pci_set_drvdata(dev, data);
	data->dev = dev;

	mic_dev_reg_data.priv = data;
	spin_lock_init(&data->lock);

	if (!(ihkd = ihk_register_device(&mic_dev_reg_data))) {
		printk(KERN_INFO "mic: Failed to register ihk driver.\n");
		return -ENOMEM;
	}
	data->ihk_dev = ihkd;

	data->alloc_desc = ihk_pagealloc_init(data->aperture_pa,
	                                      data->aperture_len,
	                                      PAGE_SIZE);
	return 0;
}

static void mic_remove(struct pci_dev *dev)
{
	struct mic_device_data *data = pci_get_drvdata(dev);
	
	printk(KERN_INFO "mic: Removing %s...\n", pci_name(dev));

	if (data) {
		ihk_pagealloc_destroy(data->alloc_desc);
		ihk_unregister_device(data->ihk_dev);
		kfree(data);
	}
}

static struct pci_driver driver = {
	.name = "mic",
	.id_table = mic_pci_ids,
	.probe = mic_probe,
	.remove = mic_remove,
};

static int __init mic_init(void)
{
	return pci_register_driver(&driver);
}

static void __exit mic_exit(void)
{
	pci_unregister_driver(&driver);
}

module_init(mic_init);
module_exit(mic_exit);

/* XXX: LICENSE */
MODULE_LICENSE("Dual BSD/GPL");

