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
#include "ops_wrappers.h"

#define DEV_MAX_MINOR 64
#define OS_MAX_MINOR 64
#define DEV_DEV_NAME "mcd"
#define OS_DEV_NAME  "mcos"

#define OS_DATA_INVALID ((void *)-1)
#define DEV_DATA_INVALID ((void *)-1)

static dev_t mcos_dev_num, mcd_dev_num;
static struct class *mcos_class, *mcd_class;

static DEFINE_SPINLOCK(dev_data_lock);
static struct aal_host_linux_device_data *dev_data[DEV_MAX_MINOR];
static int dev_max_minor = 0;

static DEFINE_SPINLOCK(os_data_lock);
static struct aal_host_linux_os_data *os_data[OS_MAX_MINOR];
static int os_max_minor = 0;

extern int ikc_master_init(aal_os_t os);
extern void ikc_master_finalize(aal_os_t os);

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
	if (!data || data == OS_DATA_INVALID) {
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
			dprintf("AAL: file not found %s\n", fn);
			return -ENOENT;
		}

		size = i_size_read(file->f_path.dentry->d_inode);
		if (size <= 0) {
			fput(file);
			dprintf("AAL: file size invalid: %lld\n", size);
			return -EINVAL;
		}

		buf = (unsigned char *)__get_free_page(GFP_KERNEL);
		if (!buf) {
			fput(file);
			return -ENOMEM;
		}

		for (done = 0; ret == 0 && done < size; ) {
			fs = get_fs();
			set_fs(get_ds());

			r = vfs_read(file, buf, PAGE_SIZE, &pos);

			set_fs(fs);
			
			if (r <= 0) {
				dprintf("vfs_read failed: %ld\n", r);
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
		if (ret == 0) {
			ret = ikc_master_init(data);
		}
	}

	return ret;
}

static int  __aal_os_shutdown(struct aal_host_linux_os_data *data, int flag)
{
	int ret = -EINVAL;
	void *buf;

	if (data->kmsg_buf) {
		buf = data->kmsg_buf;
		data->kmsg_buf = NULL;
		iounmap(data->kmsg_buf);
		__aal_os_unmap_memory(data, data->kmsg_pa, data->kmsg_len);
	}

	ikc_master_finalize(data);

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

static int __aal_os_alloc_resource(struct aal_host_linux_os_data *data,
                                   struct aal_resource *resource)
{
	if (data->ops->alloc_resource) {
		return data->ops->alloc_resource(data, data->priv, resource);
	} else { 
		return -EINVAL;
	}
}

static int __aal_os_allocate_mem(struct aal_host_linux_os_data *data,
                                 unsigned long arg)
{
	struct aal_resource resource;

	memset(&resource, 0, sizeof(resource));
	resource.memory = arg;

	return __aal_os_alloc_resource(data, &resource);
}

static int __aal_os_allocate_cpu(struct aal_host_linux_os_data *data,
                                 unsigned long arg)
{
	struct aal_resource resource;

	memset(&resource, 0, sizeof(resource));
	resource.cores = arg;

	return __aal_os_alloc_resource(data, &resource);
}

static int __aal_os_query_status(struct aal_host_linux_os_data *data)
{
	if (data->ops->query_status) {
		return data->ops->query_status(data, data->priv);
	} else { 
		return -EINVAL;
	}
}

static void __aal_os_init_kmsg(struct aal_host_linux_os_data *data)
{
	unsigned long rpa, pa, size;

	dprint_func_enter;

	if (data->kmsg_buf) {
		dprintf("data->kmsg_buf is not null: %p\n", data->kmsg_buf);
		return;
	}
	
	if (__aal_os_get_special_addr(data, AAL_SPADDR_KMSG, &rpa, &size)) {
		dprintf("get_special_addr: failed.\n");
		return;
	}
	dprint_var_x8(rpa);

	pa = __aal_os_map_memory(data, rpa, size);
	dprint_var_x8(pa);
	if ((long)pa <= 0) {
		return;
	}
	
	data->kmsg_pa = pa;
	data->kmsg_len = size;
	data->kmsg_buf = ioremap_nocache(pa, size);
	dprint_var_p(data->kmsg_buf);
}

static int __aal_os_read_kmsg(struct aal_host_linux_os_data *data,
                              char __user *buf)
{
	int tail;

	if (!data->kmsg_buf) {
		mutex_lock(&data->kmsg_mutex);
		__aal_os_init_kmsg(data);
		mutex_unlock(&data->kmsg_mutex);
	}
	if (!data->kmsg_buf) {
		return -EINVAL;
	}

	/* XXX: How to share the structure definition with manycore aal? */
	tail = *(int *)data->kmsg_buf;

	copy_to_user(buf, (char *)(data->kmsg_buf) + sizeof(int) * 2,
	             tail);

	return tail;
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

	case AAL_OS_ALLOC_CPU:
		ret = __aal_os_allocate_cpu(data, arg);
		break;

	case AAL_OS_ALLOC_MEM:
		ret = __aal_os_allocate_mem(data, arg);
		break;

	case AAL_OS_QUERY_STATUS:
		ret = __aal_os_query_status(data);
		break;

	case AAL_OS_READ_KMSG:
		ret = __aal_os_read_kmsg(data, (char * __user)arg);
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
	if (!data || data == DEV_DATA_INVALID) {
		return -EINVAL;
	}
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

static int __aal_device_create_os_init(struct aal_host_linux_device_data *data,
                                       struct aal_host_linux_os_data **os_ptr,
                                       unsigned long arg)
{
	struct aal_host_linux_os_data *os = NULL;
	struct aal_register_os_data drv_data;
	int ret;

	os = kzalloc(sizeof(*os), GFP_KERNEL);
	if (!os) {
		ret = -ENOMEM;
		goto ERR;
	}
	spin_lock_init(&os->lock);
	mutex_init(&os->kmsg_mutex);
	atomic_set(&os->refcount, 0);

	memset(&drv_data, 0, sizeof(drv_data));

	spin_lock_init(&os->listener_lock);
	spin_lock_init(&os->wait_lock);
	INIT_LIST_HEAD(&os->ikc_channels);
	INIT_LIST_HEAD(&os->wait_list);

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
	
	*os_ptr = os;

	return 0;

ERR:
	if (os) {
		kfree(os);
	}
	return ret;
}

static int __aal_device_create_os(struct aal_host_linux_device_data *data,
                                  unsigned long arg)
{
	int i, minor, ret;
	unsigned long flags;
	struct aal_host_linux_os_data *os = NULL;

	/* first check if there is any free slot */
	spin_lock_irqsave(&os_data_lock, flags);
	for (i = 0; i < os_max_minor; i++) {
		if (!os_data[i]) {
			break;
		}
	}
	if (i == os_max_minor) {
		if (os_max_minor >= OS_MAX_MINOR) {
			spin_unlock_irqrestore(&os_data_lock, flags);
			return -ENOMEM;
		}
		os_max_minor++;
	}

	minor = i;
	os_data[minor] = (void *)-1;

	spin_unlock_irqrestore(&os_data_lock, flags);

	if ((ret = __aal_device_create_os_init(data, &os, arg)) != 0) {
		os_data[minor] = NULL;
		return ret;
	}

	os->cdev.owner = THIS_MODULE;
	os->dev_num = mcos_dev_num + minor;

	if (cdev_add(&os->cdev, os->dev_num, 1) < 0) {
		/* XXX: call destroy */
		os_data[minor] = NULL;
		kfree(os);
		return -ENOMEM;
	}

	if (IS_ERR(device_create(mcos_class, NULL, os->dev_num, NULL,
	                         OS_DEV_NAME "%d", minor))) {
		/* XXX: call destroy */
		os_data[minor] = NULL;
		kfree(os);
		return -ENOMEM;
	}

	os_data[minor] = os;

	return minor;
}

static int __aal_device_destroy_os(struct aal_host_linux_device_data *data,
                                   struct aal_host_linux_os_data *os)
{
	int ret = 0;

	dprintf("__aal_device_destroy_os (%p, %p)\n", data, os);
	if (!os || os == OS_DATA_INVALID || !data || data == DEV_DATA_INVALID
	    || os->dev_data != data) {
		dprintf("%s: pointer invalid\n", __FUNCTION__);
		return -EINVAL;
	}

	if (atomic_read(&os->refcount) > 0) {
		dprintf("%s: refcount != 0\n", __FUNCTION__);
		return -EBUSY;
	}

	__aal_os_shutdown(os, FLAG_AAL_OS_SHUTDOWN_FORCE);

	if (data->ops->destroy_os) {
		ret = data->ops->destroy_os(data, data->priv, os, os->priv);
	}

	if (ret != 0) {
		return -EINVAL;
	}
	os_data[os->minor] = NULL;

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

static long aal_host_device_read(struct file *file, char __user *buf,
                                 size_t size, loff_t *off)
{
	unsigned long pa;
	void *va;
	size_t s;
	struct aal_host_linux_device_data *data = file->private_data;

	pa = aal_device_map_memory(data, *off, size);
	if ((long)pa <= 0) {
		return -EINVAL;
	}
	
	va = aal_device_map_virtual(data, pa, size, NULL, 0);
	if (!va) {
		return -ENOMEM;
	}
	s = copy_to_user(buf, va, size);
	if (s > 0) {
		s = size - s;
	} else {
		s = size;
	}
	*off += s;

	aal_device_unmap_virtual(data, va, size);

	return s;
}

static long aal_host_device_write(struct file *file, const char __user *buf,
                                  size_t size, loff_t *off)
{
	unsigned long pa;
	void *va;
	size_t s;
	struct aal_host_linux_device_data *data = file->private_data;

	pa = aal_device_map_memory(data, *off, size);
	if ((long)pa <= 0) {
		return -EINVAL;
	}
	
	va = aal_device_map_virtual(data, pa, size, NULL, 0);
	if (!va) {
		return -ENOMEM;
	}
	s = copy_from_user(va, buf, size);
	if (s > 0) {
		s = size - s;
	} else {
		s = size;
	}
	*off += s;

	aal_device_unmap_virtual(data, va, size);

	return s;
}

struct aal_host_map_data {
	int count;
	unsigned long pa;
};

static void aal_host_device_mmap_open(struct vm_area_struct *vma)
{
	struct aal_host_map_data *md = vma->vm_private_data;

	dprint_func_enter;
	md->count++;

	dprint_var_i4(md->count);
}

static void aal_host_device_mmap_close(struct vm_area_struct *vma)
{
	struct aal_host_map_data *md = vma->vm_private_data;
	struct aal_host_linux_device_data *data = vma->vm_file->private_data;

	dprint_func_enter;
	dprint_var_i4(md->count);

	if ((--md->count) > 0) {
		return;
	}

	aal_device_unmap_memory(data, md->pa, vma->vm_end - vma->vm_start);
	kfree(md);
}
	
static struct vm_operations_struct aal_host_mmap_ops = {
	.open = aal_host_device_mmap_open,
	.close = aal_host_device_mmap_close,
};

int aal_host_device_mmap(struct file *file, struct vm_area_struct *vma)
{
	unsigned long pa;
	struct aal_host_linux_device_data *data = file->private_data;
	struct aal_host_map_data *md;
	int r;

	dprint_func_enter;
	dprint_var_x8(vma->vm_pgoff);

	pa = aal_device_map_memory(data, vma->vm_pgoff << PAGE_SHIFT,
	                           vma->vm_end - vma->vm_start);
	if ((long)pa <= 0) {
		return -EINVAL;
	}
	

	r = remap_pfn_range(vma, vma->vm_start, pa >> PAGE_SHIFT,
	                    vma->vm_end - vma->vm_start,
	                    vma->vm_page_prot);
	if (r != 0) {
		return r;
	}

	vma->vm_private_data = md = kzalloc(sizeof(*md), GFP_KERNEL);
	md->pa = pa;
	md->count = 0;
	vma->vm_ops = &aal_host_mmap_ops;
		
	aal_host_device_mmap_open(vma);

	return r;
}

static struct file_operations mcd_cdev_ops = {
	.open = aal_host_device_open,
	.read = aal_host_device_read,
	.write = aal_host_device_write,
	.mmap = aal_host_device_mmap,
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

#ifndef MODULE
core_initcall(aal_host_driver_init);
#else
static int __init aal_init(void)
{
	return aal_host_driver_init();
}

static void __exit aal_exit(void)
{
	/* XXX: TODO */
	printk(KERN_ERR "This module should not be unloaded!\n");
	return;
}

module_init(aal_init);
module_exit(aal_exit);

MODULE_LICENSE("GPL v2");
#endif

/*
 * AAL public function implementations
 */
aal_device_t aal_register_device(struct aal_register_device_data *param)
{
	struct aal_host_linux_device_data *data;
	int i, minor;
	unsigned long flags;

	spin_lock_irqsave(&dev_data_lock, flags);
	for (i = 0; i < dev_max_minor; i++) {
		if (!dev_data[i]) {
			break;
		}
	}
	if (i == dev_max_minor) {
		if (dev_max_minor >= DEV_MAX_MINOR) {
			spin_unlock_irqrestore(&dev_data_lock, flags);
			return NULL;
		}
		dev_max_minor++;
	}
	minor = i;
	dev_data[i] = DEV_DATA_INVALID;
	spin_unlock_irqrestore(&dev_data_lock, flags);

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data) {
		dev_data[minor] = NULL;
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
			dev_data[minor] = NULL;
			return NULL;
		}
	}

	data->name = kstrdup(param->name, GFP_KERNEL);

	cdev_init(&data->cdev, &mcd_cdev_ops);
	data->cdev.owner = THIS_MODULE;
	data->dev_num = mcd_dev_num + minor;

	if (cdev_add(&data->cdev, data->dev_num, 1) < 0) {
		dev_data[minor] = NULL;
		return NULL;
	}
	if (IS_ERR(device_create(mcd_class, NULL, data->dev_num, NULL,
	                         DEV_DEV_NAME "%d", minor))) {
		dev_data[minor] = NULL;
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
	dev_data[data->minor] = NULL;

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

int aal_os_register_interrupt_handler(aal_os_t os, int itype,
                                      struct aal_host_interrupt_handler *h)
{
	return __aal_os_register_handler(os, itype, h);
}

int aal_os_unregister_interrupt_handler(aal_os_t os, int itype,
                                        struct aal_host_interrupt_handler *h)
{
	return __aal_os_unregister_handler(os, itype, h);
}

int aal_os_wait_for_status(aal_os_t os, enum aal_os_status status,
                           int sleepable, int timeout)
{
	return __aal_os_wait_for_status(os, status, sleepable, timeout);
}

int aal_os_get_special_address(aal_os_t os, enum aal_special_addr_type type,
                               unsigned long *pa, unsigned long *size)
{
	return __aal_os_get_special_addr(os, type, pa, size);
}

unsigned long aal_os_map_memory(aal_os_t os, unsigned long pa,
                                unsigned long size)
{
	/* XXX: PAGE_SIZE should be device-specific */
	unsigned long st, ed, offset, r;

	offset = pa & (PAGE_SIZE - 1);
	st = pa & PAGE_MASK;
	ed = (pa + size + PAGE_SIZE - 1) & PAGE_MASK;
	
	r = __aal_os_map_memory(os, st, ed - st);
	if ((long) r <= 0) {
		return r;
	}
		
	return r + offset;
}

int aal_os_unmap_memory(aal_os_t os, unsigned long pa, unsigned long size)
{
	/* XXX: PAGE_SIZE should be device-specific */
	unsigned long st, ed;

	st = pa & PAGE_MASK;
	ed = (pa + size + PAGE_SIZE - 1) & PAGE_MASK;
	
	return __aal_os_unmap_memory(os, st, ed - st);
}

int aal_os_issue_interrupt(aal_os_t os, int cpu, int vector)
{
	return __aal_os_issue_interrupt(os, cpu, vector);
}

unsigned long aal_device_map_memory(aal_device_t dev, unsigned long pa,
                                    unsigned long size)
{
	/* XXX: PAGE_SIZE should be device-specific */
	unsigned long st, ed, offset, r;

	offset = pa & (PAGE_SIZE - 1);
	st = pa & PAGE_MASK;
	ed = (pa + size + PAGE_SIZE - 1) & PAGE_MASK;
	
	r = __aal_device_map_memory(dev, st, ed - st);
	if ((long) r <= 0) {
		return r;
	}
		
	return r + offset;
}

int aal_device_unmap_memory(aal_device_t dev, unsigned long pa,
                            unsigned long size)
{
	/* XXX: PAGE_SIZE should be device-specific */
	unsigned long st, ed;

	st = pa & PAGE_MASK;
	ed = (pa + size + PAGE_SIZE - 1) & PAGE_MASK;
	
	return __aal_device_unmap_memory(dev, st, ed - st);
}

void *aal_device_map_virtual(aal_device_t dev, unsigned long pa,
                           unsigned long size, void *virtual, int flag)
{
	return __aal_device_map_virtual(dev, pa, size, virtual, flag);
}

int aal_device_unmap_virtual(aal_device_t dev, void *virtual,
                             unsigned long size)
{
	return __aal_device_unmap_virtual(dev, virtual, size);
}

aal_device_t aal_os_to_dev(aal_os_t os)
{
	return ((struct aal_host_linux_os_data *)os)->dev_data;
}

aal_device_t aal_host_find_dev(int index)
{
	if (!dev_data[index] || dev_data[index] == DEV_DATA_INVALID) {
		return NULL;
	} else{
		return dev_data[index];
	}
}

aal_os_t aal_host_find_os(int index, aal_device_t dev)
{
	if (!os_data[index] || os_data[index] == DEV_DATA_INVALID) {
		return NULL;
	} else{
		if (!dev || os_data[index]->dev_data == dev) {
			return os_data[index];
		} else {
			return NULL;
		}
	}
}
EXPORT_SYMBOL(aal_register_device);
EXPORT_SYMBOL(aal_unregister_device);
EXPORT_SYMBOL(aal_device_create_os);
EXPORT_SYMBOL(aal_os_load_file);
EXPORT_SYMBOL(aal_os_load_memory);
EXPORT_SYMBOL(aal_os_boot);
EXPORT_SYMBOL(aal_os_shutdown);
EXPORT_SYMBOL(aal_os_register_interrupt_handler);
EXPORT_SYMBOL(aal_os_unregister_interrupt_handler);
EXPORT_SYMBOL(aal_os_get_special_address);
EXPORT_SYMBOL(aal_os_wait_for_status);
EXPORT_SYMBOL(aal_host_find_dev);
EXPORT_SYMBOL(aal_host_find_os);
