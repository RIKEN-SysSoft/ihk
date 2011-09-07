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

static struct aal_os_ops knf_aal_os_ops = {
	.load_mem = knf_aal_os_load_mem,
	.load_file = knf_aal_os_load_file,
	.boot = knf_aal_os_boot,
	.shutdown = knf_aal_os_shutdown,
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

long knf_aal_debug_request(aal_device_t aal_dev, void *priv,
                             unsigned int r, unsigned long arg)
{
	struct knf_device_data *kdd = priv;

	return __knf_debug_request(kdd, r, arg);
}

static struct aal_device_ops knf_aal_device_ops = {
	.init = knf_aal_init,
	.exit = knf_aal_exit,
	.create_os = knf_aal_create_os,
	.debug_request = knf_aal_debug_request,
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

	return 0;
}

static void knf_remove(struct pci_dev *dev)
{
	struct knf_device_data *data = pci_get_drvdata(dev);
	
	printk(KERN_INFO "knf: Removing %s...\n", pci_name(dev));

	if (data) {
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

