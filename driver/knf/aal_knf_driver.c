/*
 * AAL Knights Ferry Driver
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
#include <aal/aal_host_driver.h>
#include <aal/aal_host_misc.h>
#include <aal/misc/debug.h>
#include "knf.h"

static struct pci_device_id knf_pci_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2240), },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2241), },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2242), },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2243), },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2249), },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x224a), },
	{ 0, }
};

/**** OS Section ****/
extern int __knf_prepare_os_load(struct knf_device_data *kdd);
extern int __knf_load_os_file(struct knf_device_data *kdd, const char *fn);
extern int knf_boot_os(struct knf_device_data *kdd);
extern void knf_shutdown(struct knf_device_data *kdd);
extern int __knf_os_get_status(struct knf_device_data *kdd);
extern int knf_issue_interrupt(struct knf_device_data *kdd,
                               int apicid, int vector);
extern int knf_map_aperture(struct knf_device_data *kdd,
                            unsigned long ap_address, unsigned long phys,
                            int npages);
extern int knf_unmap_aperture(struct knf_device_data *kdd, 
                              unsigned long ap_address, int npages);
extern int __knf_get_special_addr(struct knf_device_data *kdd, 
                                  enum aal_special_addr_type type,
                                  unsigned long *addr, unsigned long *size);

static int knf_aal_os_boot(aal_os_t aal_os, void *priv, int flag)
{
	struct knf_os_data *os = priv;
	struct knf_device_data *kdd = os->dev;

	return knf_boot_os(kdd);
}

static int knf_aal_os_load_file(aal_os_t aal_os, void *priv, const char *fn)
{
	struct knf_os_data *os = priv;
	struct knf_device_data *kdd = os->dev;

	return __knf_load_os_file(kdd, fn);
}

static int knf_aal_os_load_mem(aal_os_t aal_os, void *priv, const char *buf,
                               unsigned long size, long offset)
{
	struct knf_os_data *os = priv;
	struct knf_device_data *kdd = os->dev;

	if (offset + size > kdd->aperture_len) {
		return -ENOMEM;
	}
	memcpy(kdd->aperture_va + offset, buf, size);

	return 0;
}

static int knf_aal_os_shutdown(aal_os_t aal_os, void *priv, int flag)
{
	struct knf_os_data *os = priv;
	struct knf_device_data *kdd = os->dev;

	knf_shutdown(kdd);
	
	return 0;
}

static int knf_aal_os_alloc_resource(aal_os_t aal_os, void *priv,
                                     struct aal_resource *resource)
{
/*	struct mee_os_data *os = priv; */

	/*
	 * XXX: We assume that only one kernel is running.
	 * So succeeding in creating an OS means allocating all the resource
	 * on the card.
	 */

	return 0;
}

static enum aal_os_status knf_aal_os_query_status(aal_os_t aal_os, void *priv)
{
	struct knf_os_data *os = priv;
	struct knf_device_data *kdd = os->dev;
	int v;

	/* XXX: Before booting, this should be maintained by this driver */
	v = __knf_os_get_status(kdd);
	if (v == 0) {
		return AAL_OS_STATUS_BOOTING;
	} else if (v == 1) {
		return AAL_OS_STATUS_BOOTED;
	} else if (v == 2) {
		return AAL_OS_STATUS_READY;
	}
	return AAL_OS_STATUS_NOT_BOOTED;
}

int knf_aal_os_issue_interrupt(aal_os_t aal_os, void *priv, int cpu, int vector)
{
	struct knf_os_data *os = priv;
	struct knf_device_data *kdd = os->dev;

	/* XXX: cpu to apic id */
	return knf_issue_interrupt(kdd, cpu, vector);
}

static unsigned long knf_aal_os_map_memory(aal_os_t aal_os, void *priv,
                                           unsigned long remote_phys,
                                           unsigned long size)
{
	struct knf_os_data *os = priv;
	struct knf_device_data *kdd = os->dev;
	unsigned long phys;

	dprint_func_enter;
	dprint_var_x8(size);

	size = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;

	if (!(phys = aal_pagealloc_alloc(kdd->alloc_desc, size))) {
		return (unsigned long)-ENOMEM;
	}
	
	if (knf_map_aperture(kdd, phys, remote_phys, size)) {
		aal_pagealloc_free(kdd->alloc_desc, phys, size);
		return (unsigned long)-ENOMEM;
	}

	return phys;
}

static int knf_aal_os_unmap_memory(aal_os_t aal_os, void *priv,
                                   unsigned long phys, unsigned long size)
{
	struct knf_os_data *os = priv;
	struct knf_device_data *kdd = os->dev;

	size = (size + PAGE_SIZE) >> PAGE_SHIFT;

	knf_unmap_aperture(kdd, phys, size);
	aal_pagealloc_free(kdd->alloc_desc, phys, size);

	return 0;
}

static int knf_aal_os_get_special_addr(aal_os_t aal_os, void *priv,
                                       enum aal_special_addr_type type,
                                       unsigned long *addr,
                                       unsigned long *size)
{
	int v;
	struct knf_os_data *os = priv;
	struct knf_device_data *kdd = os->dev;

	v = __knf_os_get_status(kdd);
	if (v < 1) {
		return -EBUSY;
	}

	return __knf_get_special_addr(kdd, type, addr, size);
}

static struct aal_os_ops knf_aal_os_ops = {
	.load_mem = knf_aal_os_load_mem,
	.load_file = knf_aal_os_load_file,

	.boot = knf_aal_os_boot,
	.shutdown = knf_aal_os_shutdown,

	.alloc_resource = knf_aal_os_alloc_resource,
	.query_status = knf_aal_os_query_status,
	.issue_interrupt = knf_aal_os_issue_interrupt,

	.map_memory = knf_aal_os_map_memory,
	.unmap_memory = knf_aal_os_unmap_memory,

	.get_special_addr = knf_aal_os_get_special_addr,
};	

static struct aal_register_os_data knf_os_reg_data = {
	.name = "knfos_base",
	.flag = 0,
	.ops = &knf_aal_os_ops,
};

/**** Device Section ****/

extern int knf_device_init(struct pci_dev *dev, struct knf_device_data *data);
extern void knf_device_destroy(struct pci_dev *dev,
                               struct knf_device_data *data);
extern long __knf_debug_request(struct knf_device_data *kdd, 
                                int r, unsigned long arg);

static int knf_aal_init(aal_device_t aal_dev, void *priv)
{
	struct knf_device_data *data = priv;

	return knf_device_init(data->dev, data);
}

static int knf_aal_exit(aal_device_t aal_dev, void *priv)
{
	struct knf_device_data *data = priv;

	knf_device_destroy(data->dev, data);

	return 0;
}

static int knf_aal_create_os(aal_device_t aal_dev, void *priv,
                             unsigned long arg, aal_os_t aal_os,
                             struct aal_register_os_data *regdata)
{
	unsigned long flags;
	struct knf_device_data *data = priv;
	struct knf_os_data *os;

	/* Allocate a device. First kernel should be one */
	spin_lock_irqsave(&data->lock, flags);
	if (data->status != 0 && data->status != 2) {
		spin_unlock_irqrestore(&data->lock, flags);
		return -EBUSY;
	}
	data->status = 1;
	spin_unlock_irqrestore(&data->lock, flags);

	*regdata = knf_os_reg_data;
	os = kzalloc(sizeof(struct knf_os_data), GFP_KERNEL);
	if (!os) {
		data->status = 0; /* No other one should reach here */
		return -ENOMEM;
	}
	spin_lock_init(&os->lock);
	os->dev = data;

	if (__knf_prepare_os_load(data) != 0) {
		data->status = 0;
		return -EBUSY;
	}

	regdata->priv = os;

	return 0;
}

static long knf_aal_debug_request(aal_device_t aal_dev, void *priv,
                                  unsigned int r, unsigned long arg)
{
	struct knf_device_data *kdd = priv;

	return __knf_debug_request(kdd, r, arg);
}

static void *knf_aal_map_virtual(aal_device_t aal_dev, void *priv,
                                 unsigned long phys, unsigned long size,
                                 void *virt, int flags)
{
	struct knf_device_data *kdd = priv;
	
	if (!virt && (flags & AAL_MAP_FLAG_NOCACHE) && phys >= kdd->aperture_pa
	    && phys + size < kdd->aperture_pa + kdd->aperture_len) {
		return ((char *)kdd->aperture_va) + (phys - kdd->aperture_pa);
	}
	return aal_host_map_generic(aal_dev, phys, virt, size, flags);
}

static int knf_aal_unmap_virtual(aal_device_t aal_dev, void *priv,
                                  void *virt, unsigned long size)
{
	struct knf_device_data *kdd = priv;

	if (virt >= kdd->aperture_va
	    && (unsigned char *)virt < ((unsigned char *)kdd->aperture_va
	                                + kdd->aperture_len)) {
		return 0;
	}

	return aal_host_unmap_generic(aal_dev, virt, size);
}

static struct aal_device_ops knf_aal_device_ops = {
	.init = knf_aal_init,
	.exit = knf_aal_exit,
	.create_os = knf_aal_create_os,
	.debug_request = knf_aal_debug_request,
	.map_virtual = knf_aal_map_virtual,
	.unmap_virtual = knf_aal_unmap_virtual,
};	

static struct aal_register_device_data knf_dev_reg_data = {
	.name = "knf",
	.flag = 0,
	.ops = &knf_aal_device_ops,
};

static int knf_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	struct knf_device_data *data;
	aal_device_t aald;

	printk(KERN_INFO "knf: Found at %s. (vendor = %x, device = %x)\n",
	       pci_name(dev), id->vendor, id->device);

	data = kzalloc(sizeof(struct knf_device_data), GFP_KERNEL);
	if (!data) {
		return -ENOMEM;
	}
	pci_set_drvdata(dev, data);
	data->dev = dev;

	knf_dev_reg_data.priv = data;
	spin_lock_init(&data->lock);

	if (!(aald = aal_register_device(&knf_dev_reg_data))) {
		printk(KERN_INFO "knf: Failed to register aal driver.\n");
		return -ENOMEM;
	}
	data->aal_dev = aald;

	data->alloc_desc = aal_pagealloc_init(data->aperture_pa,
	                                      data->aperture_len,
	                                      PAGE_SIZE);
	return 0;
}

static void knf_remove(struct pci_dev *dev)
{
	struct knf_device_data *data = pci_get_drvdata(dev);
	
	printk(KERN_INFO "knf: Removing %s...\n", pci_name(dev));

	if (data) {
		aal_pagealloc_destroy(data->alloc_desc);
		aal_unregister_device(data->aal_dev);
		kfree(data);
	}
}

static struct pci_driver driver = {
	.name = "knf",
	.id_table = knf_pci_ids,
	.probe = knf_probe,
	.remove = knf_remove,
};

static int __init knf_init(void)
{
	return pci_register_driver(&driver);
}

static void __exit knf_exit(void)
{
	pci_unregister_driver(&driver);
}

module_init(knf_init);
module_exit(knf_exit);

MODULE_LICENSE("GPL");

