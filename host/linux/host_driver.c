/*
 * Manycore Abstraction Layer - Drivers for User Process Interaction
 * (C) Copyright 2011 Taku Shimosawa.
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
#include <linux/file.h>
#include <aal/aal_host_user.h>
#include <aal/aal_host_driver.h>
#include <aal/misc/debug.h>
#include "host_linux.h"

#define DEV_MAX_MINOR 64
#define OS_MAX_MINOR 64
#define DEV_DEV_NAME "mcd"
#define OS_DEV_NAME  "mcos"

static dev_t mcos_dev_num, mcd_dev_num;
static struct class *mcos_class, *mcd_class;

static DEFINE_SPINLOCK(dev_data_lock);
static struct aal_host_linux_device_data *dev_data[DEV_MAX_MINOR];
static int dev_max_minor = 0;

static DEFINE_SPINLOCK(os_data_lock);
static struct aal_host_linux_os_data *os_data[OS_MAX_MINOR];
static int os_max_minor = 0;

/*
 * OS character device file operations.
 */
static int aal_host_os_open(struct inode *inode, struct file *file)
{
	int idx, ret;
	struct aal_host_linux_os_data *data;

	idx = inode->i_rdev - mcos_dev_num;
	if (idx < 0 || idx > os_max_minor) {
		return -EINVAL;
	}

	data = os_data[idx];
	if (!data) {
		return -ENOENT;
	}
	if (data->flag & AAL_OS_FLAG_SHARABLE) {
		atomic_inc(&data->refcount);
	} else if (atomic_cmpxchg(&data->refcount, 0, 1) != 0) {
		return -EBUSY;
	}

	file->private_data = data;

	if (data->ops->open) {
		ret = data->ops->open(data, data->priv, file);
		if (ret != 0) {
			atomic_dec(&data->refcount);
			return ret;
		}
	}

	return 0;
}

static int aal_host_os_release(struct inode *inode, struct file *file)
{
	struct aal_host_linux_os_data *data;
	
	data = file->private_data;

	if (data->ops->close) {
		data->ops->close(data, data->priv, file);
	}

	atomic_dec(&data->refcount);
	
	return 0;
}

static int __aal_os_load_memory(struct aal_host_linux_os_data *data,
                                char *buf, unsigned long size, long offset)
{
	if (data->ops->load_mem) {
		return data->ops->load_mem(data, data->priv, buf, size, offset);
	} else{
		return -EINVAL;
	}
}

static int __aal_os_load_file(struct aal_host_linux_os_data *data, char *fn)
{
	char *buf;
	struct file *file;
	int ret = 0;
	loff_t size, done, pos = 0;
	long r;
	mm_segment_t fs;

	if (data->ops->load_file) {
		dprintf("AAL: os_load_file is defined. Use it.\n");

		ret = data->ops->load_file(data, data->priv, fn);
	} else if (data->ops->load_mem){
		dprintf("AAL: os_load_mem is defined. Use it.\n");

		file = filp_open(fn, O_RDONLY, 0);
		if (IS_ERR(file)) {
			return -ENOENT;
		}

		size = i_size_read(file->f_path.dentry->d_inode);
		if (size <= 0) {
			fput(file);
			return -EINVAL;
		}

		buf = (unsigned char *)__get_free_page(GFP_KERNEL);
		if (!buf) {
			ret = -ENOMEM;
		}

		for (done = 0; ret == 0 && done < size; ) {
			fs = get_fs();
			set_fs(get_ds());

			r = vfs_read(file, buf, PAGE_SIZE, &pos);

			set_fs(fs);
			
			if (r <= 0) {
				ret = (int)r;
				break;
			}

			ret = __aal_os_load_memory(data, buf, r, done);

			done += r;
		}

		fput(file);
	} else {
		dprintf("AAL: No loading function is defined.\n");
		ret = -EINVAL;
	}

	return ret;
}

static int __aal_os_ioctl_load(struct aal_host_linux_os_data *data,
                               char * __user filename)
{
	char *fn;
	int ret;

	/* XXX: 256 is too arbitary */
	fn = strndup_user(filename, 256);
	if (!fn) {
		return -ENOMEM;
	}

	ret = __aal_os_load_file(data, fn);
	kfree(fn);

	return ret;
}

static int  __aal_os_boot(struct aal_host_linux_os_data *data, int flag)
{
	int ret = -EINVAL;

	if (data->ops->boot) {
		ret = data->ops->boot(data, data->priv, flag);
	}

	return ret;
}

static int  __aal_os_shutdown(struct aal_host_linux_os_data *data, int flag)
{
	int ret = -EINVAL;

	if (data->ops->shutdown) {
		ret = data->ops->shutdown(data, data->priv, flag);
	}
	return ret;
}

static int __aal_os_ioctl_debug_request(struct aal_host_linux_os_data *data,
                                        unsigned int request,
                                        unsigned long arg)
{
	int ret;

	if (data->ops->debug_request) {
		ret = data->ops->debug_request(data, data->priv, request, arg);
	} else {
		ret = -EINVAL;
	}

	return ret;
}

static long aal_host_os_ioctl(struct file *file, unsigned int request,
                              unsigned long arg)
{
	int ret = -EINVAL;
	struct aal_host_linux_os_data *data;
	
	data = file->private_data;

	dprintf("AAL: ioctl request = %x, arg = %lx\n", request, arg);

	switch (request) {
	case AAL_OS_LOAD:
		ret = __aal_os_ioctl_load(data, (char * __user)arg);
		break;

	case AAL_OS_BOOT:
		ret = __aal_os_boot(data, arg);
		break;

	case AAL_OS_SHUTDOWN:
		ret = __aal_os_shutdown(data, arg);
		break;

	default:
		if (request >= AAL_OS_DEBUG_START && 
		    request <= AAL_OS_DEBUG_END) {
			ret = __aal_os_ioctl_debug_request(data,
			                                   request, arg);
		}
		break;
	}

	return ret;
}

static struct file_operations mcos_cdev_ops = {
	.open = aal_host_os_open,
	.unlocked_ioctl = aal_host_os_ioctl,
	.release = aal_host_os_release,
};

/*
 * Device character device file operations.
 */
static int aal_host_device_open(struct inode *inode, struct file *file)
{
	int idx, ret;
	struct aal_host_linux_device_data *data;

	idx = inode->i_rdev - mcd_dev_num;
	if (idx < 0 || idx > dev_max_minor) {
		return -EINVAL;
	}

	data = dev_data[idx];
	if (data->flag & AAL_DEVICE_FLAG_SHARABLE) {
		atomic_inc(&data->refcount);
	} else if (atomic_cmpxchg(&data->refcount, 0, 1) != 0) {
		return -EBUSY;
	}

	file->private_data = data;

	if (data->ops->open) {
		ret = data->ops->open(data, data->priv, file);
		if (ret != 0) {
			atomic_dec(&data->refcount);
			return ret;
		}
	}

	return 0;
}

static int aal_host_device_release(struct inode *inode, struct file *file)
{
	struct aal_host_linux_device_data *data;
	
	data = file->private_data;

	if (data->ops->close) {
		data->ops->close(data, data->priv, file);
	}

	atomic_dec(&data->refcount);
	
	return 0;
}

static int __aal_device_ioctl_debug_request(struct aal_host_linux_device_data *
                                            data,
                                            unsigned int request,
                                            unsigned long arg)
{
	int ret;

	if (data->ops->debug_request) {
		ret = data->ops->debug_request(data, data->priv, request, arg);
	} else {
		ret = -EINVAL;
	}

	return ret;
}

static int __aal_device_create_os(struct aal_host_linux_device_data *data,
                                  unsigned long arg)
{
	int minor, ret;
	struct aal_host_linux_os_data *os = NULL;
	struct aal_register_os_data drv_data;
	unsigned long flags;

	spin_lock_irqsave(&os_data_lock, flags);
	minor = os_max_minor;
	if (minor >= OS_MAX_MINOR) {
		spin_unlock_irqrestore(&os_data_lock, flags);
		return -ENOMEM;
	}
	os_max_minor++;
	spin_unlock_irqrestore(&os_data_lock, flags);

	os = kzalloc(sizeof(*os), GFP_KERNEL);
	if (!os) {
		ret = -ENOMEM;
		goto ERR;
	}
	spin_lock_init(&os->lock);
	atomic_set(&os->refcount, 0);

	memset(&drv_data, 0, sizeof(drv_data));
	
	if (data->ops->create_os && 
	    (ret = data->ops->create_os(data, data->priv, arg, 
	                                os, &drv_data))) {
		goto ERR;
	}

	os->name = drv_data.name;
	os->flag = drv_data.flag;
	os->ops = drv_data.ops;
	os->priv = drv_data.priv;
	os->dev_data = data;

	cdev_init(&os->cdev, &mcos_cdev_ops);
	
	os_data[minor] = os;

	os->cdev.owner = THIS_MODULE;
	os->dev_num = mcos_dev_num + minor;

	if (cdev_add(&os->cdev, os->dev_num, 1) < 0) {
		ret = -ENOMEM;
		goto ERR;
	}

	if (IS_ERR(device_create(mcos_class, NULL, os->dev_num, NULL,
	                         OS_DEV_NAME "%d", minor))) {
		ret = -ENOMEM;
		goto ERR;
	}

	return minor;

ERR:
	if (os) {
		kfree(os);
	}

	spin_lock_irqsave(&os_data_lock, flags);
	if (os_max_minor == minor + 1) {
		os_max_minor--;
	}
	spin_unlock_irqrestore(&os_data_lock, flags);
	
	return ret;
}

static int __aal_device_destroy_os(struct aal_host_linux_device_data *data,
                                   struct aal_host_linux_os_data *os)
{
	int ret;

	if (!os || !data || os->dev_data != data) {
		return -EINVAL;
	}

	if (atomic_read(&os->refcount) > 0) {
		return -EBUSY;
	}

	__aal_os_shutdown(os, FLAG_AAL_OS_SHUTDOWN_FORCE);

	if (data->ops->destroy_os) {
		ret = data->ops->destroy_os(data, data->priv, os, os->priv);
	}
	if (ret != 0) {
		return -EINVAL;
	}

	cdev_del(&os->cdev);
	device_destroy(mcos_class, os->dev_num);

	return 0;
}

static int __destroy_all_os(struct aal_host_linux_device_data *data)
{
	unsigned long flags;
	int i, r;
	struct aal_host_linux_os_data *os;

	/*
	 * We assume that the newer OS is allocated in the higher index 
	 */
	spin_lock_irqsave(&os_data_lock, flags);
	for (i = 0; i < os_max_minor; i++) {
		if (os_data[i] && os_data[i]->dev_data == data) {
			os = os_data[i];
			os_data[i] = NULL;
			spin_unlock_irqrestore(&os_data_lock, flags);

			r = __aal_device_destroy_os(data, os);
			if (r != 0) {
				spin_lock_irqsave(&os_data_lock, flags);
				/* XXX: Check if we can return the value */
				os_data[i] = os;
				spin_unlock_irqrestore(&os_data_lock, flags);

				return r;
			}

			spin_lock_irqsave(&os_data_lock, flags);
		}
	}
	spin_unlock_irqrestore(&os_data_lock, flags);

	return 0;
}

static long aal_host_device_ioctl(struct file *file, unsigned int request,
                                  unsigned long arg)
{
	int ret = -EINVAL;
	struct aal_host_linux_device_data *data;
	
	data = file->private_data;

	switch (request) {
	case AAL_DEVICE_CREATE_OS:
		ret = __aal_device_create_os(data, arg);
		break;

	default:
		if (request >= AAL_DEVICE_DEBUG_START && 
		    request <= AAL_DEVICE_DEBUG_END) {
			ret = __aal_device_ioctl_debug_request(data,
			                                       request, arg);
		}
		break;
	}

	return ret;
}

static struct file_operations mcd_cdev_ops = {
	.open = aal_host_device_open,
	.unlocked_ioctl = aal_host_device_ioctl,
	.release = aal_host_device_release,
};

/*
 * Initialization function for AAL Host Driver.
 * It prepares character devices, but does not create actual device files.
 * (It pends creating until the hardware driver is registered)
 */
static int __init aal_host_driver_init(void)
{
	if (alloc_chrdev_region(&mcd_dev_num, 0, DEV_MAX_MINOR, 
	                        DEV_DEV_NAME) < 0) {
		printk(KERN_INFO "AAL: Cannot allocate char device number.\n");
		goto ERR;
	}
	
	mcd_class = class_create(THIS_MODULE, DEV_DEV_NAME);
	if (!mcd_class) {
		printk(KERN_INFO "AAL: Cannot create mcd.\n");
		goto ERR;
	}
	
	if (alloc_chrdev_region(&mcos_dev_num, 0, OS_MAX_MINOR,
	                        OS_DEV_NAME) < 0) {
		printk(KERN_INFO "AAL: Cannot allocate char device number.\n");
		goto ERR;
	}
	mcos_class = class_create(THIS_MODULE, OS_DEV_NAME);
	if (!mcos_class) {
		printk(KERN_INFO "AAL: Cannot create mcos.\n");
		goto ERR;
	}

	printk("AAL Initialized: Device number: Device %x, OS %x\n",
	       mcd_dev_num, mcos_dev_num);

	return 0;
ERR:
	if (mcos_class)
		class_destroy(mcos_class);
	if (mcos_dev_num)
		unregister_chrdev_region(mcos_dev_num, OS_MAX_MINOR);
	if (mcd_class)
		class_destroy(mcd_class);
	if (mcd_dev_num)
		unregister_chrdev_region(mcd_dev_num, DEV_MAX_MINOR);

	return -ENOMEM;
}
core_initcall(aal_host_driver_init);

/*
 * AAL public function implementations
 */
aal_device_t aal_register_device(struct aal_register_device_data *param)
{
	struct aal_host_linux_device_data *data;
	int minor;
	unsigned long flags;

	spin_lock_irqsave(&dev_data_lock, flags);
	minor = dev_max_minor;
	if (minor >= DEV_MAX_MINOR) {
		spin_unlock_irqrestore(&dev_data_lock, flags);
		return NULL;
	}
	dev_max_minor++;
	spin_unlock_irqrestore(&dev_data_lock, flags);

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data) {
		return NULL;
	}

	spin_lock_init(&data->lock);
	atomic_set(&data->refcount, 0);
	data->flag = param->flag;
	data->ops = param->ops;
	data->priv = param->priv;

	if (param->ops->init) {
		if (param->ops->init(data, data->priv) != 0) {
			spin_lock_irqsave(&dev_data_lock, flags);
			if (minor + 1 == os_max_minor) {
				os_max_minor--;
			}
			spin_unlock_irqrestore(&dev_data_lock, flags);

			kfree(data);
			return NULL;
		}
	}

	data->name = kstrdup(param->name, GFP_KERNEL);

	cdev_init(&data->cdev, &mcd_cdev_ops);
	data->cdev.owner = THIS_MODULE;
	data->dev_num = mcd_dev_num + minor;

	if (cdev_add(&data->cdev, data->dev_num, 1) < 0) {
		return NULL;
	}
	if (IS_ERR(device_create(mcd_class, NULL, data->dev_num, NULL,
	                         DEV_DEV_NAME "%d", minor))) {
		return NULL;
	}

	dev_data[minor] = data;

	printk("AAL: Device %s registered. /dev/%s%d created.\n",
	       data->name, DEV_DEV_NAME, minor);

	return data;
}

int aal_unregister_device(aal_device_t aaldev)
{
	struct aal_host_linux_device_data *data = aaldev;

	if (atomic_read(&data->refcount) > 0) {
		return -EBUSY;
	}
	if (__destroy_all_os(data) != 0) {
		return -EBUSY;
	}

	cdev_del(&data->cdev);
	device_destroy(mcd_class, data->dev_num);

	if (data->ops->exit) {
		data->ops->exit(data, data->priv);
	}

	printk("AAL: Device %s unregistered.\n", data->name);
	kfree(data);

	return 0;
}

aal_os_t aal_device_create_os(aal_device_t data, unsigned long arg)
{
	int ret;

	ret = __aal_device_create_os(data, arg);
	if (ret >= 0) {
		return os_data[ret];
	} else {
		return NULL;
	}
}

int aal_os_boot(aal_os_t os, int flag)
{
	return __aal_os_boot(os, flag);
}

int aal_os_shutdown(aal_os_t os, int flag)
{
	return __aal_os_shutdown(os, flag);
}

int aal_os_load_memory(aal_os_t os, char *buf, unsigned long size,
                       long offset) {
	return __aal_os_load_memory(os, buf, size, offset);
}

int aal_os_load_file(aal_os_t os, char *fn) {
	return __aal_os_load_file(os, fn);
}

EXPORT_SYMBOL(aal_register_device);
EXPORT_SYMBOL(aal_unregister_device);
EXPORT_SYMBOL(aal_device_create_os);
EXPORT_SYMBOL(aal_os_load_file);
EXPORT_SYMBOL(aal_os_load_memory);
EXPORT_SYMBOL(aal_os_boot);
EXPORT_SYMBOL(aal_os_shutdown);

