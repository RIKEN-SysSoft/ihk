/**
 * \file host_driver.c
 * 
 * \brief 
 * IHK-Host: Character Device Drivers for User Process Interaction
 * Character device implementation in Linux of IHK-Host device and OS driver
 * files.
 *
 * \author Taku Shimosawa  <shimosawa@is.s.u-tokyo.ac.jp> \par
 * Copyright (C) 2011 - 2012  Taku Shimosawa
 * 
 * \author Balazs Gerofi  <bgerofi@il.is.s.u-tokyo.ac.jp> \par
 * Copyright (C) 2013  The University Of Tokyo
 *
 * HISTORY:
 *  2013/06/24: bgerofi - interface for querying free physical memory on OS
 *
 */
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/init.h>
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
#include <linux/string.h>
#include <linux/eventfd.h>
#include <linux/version.h>
#include <linux/cred.h>
#include <ihk/ihk_host_user.h>
#include <ihk/ihk_host_driver.h>
#include <asm/spinlock.h>
#include <ihk/misc/debug.h>
#include "host_linux.h"
#include "ops_wrappers.h"
#include <config.h>

//#define DEBUG_IKC

#ifdef DEBUG_IKC
#define dkprintf(...) kprintf(__VA_ARGS__)
#define ekprintf(...) kprintf(__VA_ARGS__)
#else
#define dkprintf(...) do { if (0) printk(__VA_ARGS__); } while (0)
#define ekprintf(...) printk(__VA_ARGS__)
#endif


#define DEV_MAX_MINOR 64
#define OS_MAX_MINOR 64
#define DEV_DEV_NAME "mcd"
#define OS_DEV_NAME  "mcos"

#define OS_DATA_INVALID ((void *)-1)
#define DEV_DATA_INVALID ((void *)-1)

static dev_t mcos_dev_num, mcd_dev_num;
static struct class *mcos_class, *mcd_class;

static DEFINE_SPINLOCK(dev_data_lock);
static struct ihk_host_linux_device_data *dev_data[DEV_MAX_MINOR];
static int dev_max_minor = 0;

static DEFINE_SPINLOCK(os_data_lock);
static struct ihk_host_linux_os_data *os_data[OS_MAX_MINOR];
static int os_max_minor = 0;

static struct list_head ihk_os_notifiers;
DEFINE_SEMAPHORE(ihk_os_notifiers_lock);

static struct list_head ihk_kmsg_bufs;
static spinlock_t ihk_kmsg_bufs_lock;

extern int ihk_ikc_master_init(ihk_os_t os);
extern void ikc_master_finalize(ihk_os_t os);

struct ihk_event {
	struct list_head list;
	int type;
	struct eventfd_ctx *event;
};
#define IHK_OS_MONITOR_KERNEL_FREEZING 8
#define IHK_OS_MONITOR_KERNEL_FROZEN 9
#define IHK_OS_MONITOR_KERNEL_THAW 10

/*
 * OS character device file operations.
 */
/** \brief open operation for an OS file */
static int ihk_host_os_open(struct inode *inode, struct file *file)
{
	int idx, ret;
	struct ihk_host_linux_os_data *data;
	struct ihk_file *ifile;

	idx = inode->i_rdev - mcos_dev_num;
	if (idx < 0 || idx > os_max_minor) {
		return -EINVAL;
	}

	data = os_data[idx];
	if (!data || data == OS_DATA_INVALID) {
		return -ENOENT;
	}
	if (data->flag & IHK_OS_FLAG_SHARABLE) {
		atomic_inc(&data->refcount);
	} else if (atomic_cmpxchg(&data->refcount, 0, 1) != 0) {
		return -EBUSY;
	}

	ifile = kmalloc(sizeof(struct ihk_file), GFP_KERNEL);
	memset(ifile, '\0', sizeof(struct ihk_file));
	ifile->osdata = data;
	file->private_data = ifile;

	if (data->ops->open) {
		ret = data->ops->open(data, data->priv, file);
		if (ret != 0) {
			atomic_dec(&data->refcount);
			return ret;
		}
	}

	return 0;
}

/** \brief close operation for an OS device file */
static int ihk_host_os_release(struct inode *inode, struct file *file)
{
	struct ihk_host_linux_os_data *data;
	struct ihk_file *ifile;

	ifile = file->private_data;
	data = ifile->osdata;
	if(ifile->release_handler){
		ifile->release_handler(data, ifile->param);
	}

	if (data->ops->close) {
		data->ops->close(data, data->priv, file);
	}

	atomic_dec(&data->refcount);
	kfree(ifile);
	
	return 0;
}

void  ihk_os_register_release_handler(struct file *file,
                                      void (*handler)(ihk_os_t, void *),
                                      void *param)
{
	struct ihk_file *ifile = file->private_data;

	ifile->release_handler = handler;
	ifile->param = param;
}

void ihk_os_set_mcos_private_data(struct file *file, void *data)
{
	struct ihk_file *ifile = file->private_data;

	ifile->mcos_data = data;
}

void *ihk_os_get_mcos_private_data(struct file *file)
{
	struct ihk_file *ifile = file->private_data;

	return ifile->mcos_data;
}

/** \brief load_memory operation for an OS device file */
static int __ihk_os_load_memory(struct ihk_host_linux_os_data *data,
                                char *buf, unsigned long size, long offset)
{
	if (data->ops->load_mem) {
		return data->ops->load_mem(data, data->priv, buf, size, offset);
	} else{
		return -EINVAL;
	}
}

/** \brief load_file operation for an OS device file
 *
 * This function is called when a user requests to load the kernel image
 * directly from a file.
 * If the IHK OS driver does not provide a handler for load_file,
 * it uses the load_mem handler instead.
 */
static int __ihk_os_load_file(struct ihk_host_linux_os_data *data, char *fn)
{
	char *buf;
	struct file *file;
	int ret = 0;
	loff_t size, done, pos = 0;
	long r;

	if (data->ops->load_file) {
		dprintf("IHK: os_load_file is defined. Use it.\n");

		ret = data->ops->load_file(data, data->priv, fn);
	} else if (data->ops->load_mem){
		dprintf("IHK: os_load_mem is defined. Use it.\n");

		file = filp_open(fn, O_RDONLY, 0);
		if (IS_ERR(file)) {
			dprintf("IHK: file not found %s\n", fn);
			return -ENOENT;
		}

		size = i_size_read(file->f_path.dentry->d_inode);
		if (size <= 0) {
			fput(file);
			dprintf("IHK: file size invalid: %lld\n", size);
			return -EINVAL;
		}

		buf = (unsigned char *)__get_free_page(GFP_KERNEL);
		if (!buf) {
			fput(file);
			return -ENOMEM;
		}

		for (done = 0; ret == 0 && done < size; ) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)
			r = kernel_read(file, buf, PAGE_SIZE, &pos);
#else
			r = kernel_read(file, pos, buf, PAGE_SIZE);
			pos += r;
#endif
			if (r <= 0) {
				dprintf("kernel_read failed: %ld\n", r);
				ret = (int)r;
				break;
			}

			ret = __ihk_os_load_memory(data, buf, r, done);

			done += r;
		}

		fput(file);
	} else {
		dprintf("IHK: No loading function is defined.\n");
		ret = -EINVAL;
	}

	return ret;
}

/** \brief ioctl handler for a load-file request */
static int __ihk_os_ioctl_load(struct ihk_host_linux_os_data *data,
                               char * __user filename)
{
	char *fn;
	int ret;

	/* XXX: 256 is too arbitary */
	fn = strndup_user(filename, 256);
	if (!fn) {
		return -ENOMEM;
	}

	ret = __ihk_os_load_file(data, fn);
	kfree(fn);

	return ret;
}

/** \brief Boot a kernel related to the OS file */
static int  __ihk_os_boot(struct ihk_host_linux_os_data *data, int flag)
{
	int ret = -EINVAL;
	int index = ihk_host_os_get_index(data);
	int found = 0;
	struct ihk_kmsg_buf_container *cont;
	unsigned long flags;

	/* Get the latest kmsg_buf */
	spin_lock_irqsave(&ihk_kmsg_bufs_lock, flags);
	list_for_each_entry_reverse(cont, &ihk_kmsg_bufs, list) {
		if (cont->os_index == data->minor) {
			data->kmsg_buf_container = cont;
			dkprintf("%s: got kmsg_buf %p\n", __FUNCTION__, cont);
			atomic_inc(&cont->count); /* OS instance is referring to it */
			found = 1;
			break;
		}
	}
	spin_unlock_irqrestore(&ihk_kmsg_bufs_lock, flags);
	
	if (!found) {
		return -EINVAL;
	}

	/*
	 * Take OS notifiers lock here so that we can safely
	 * return on a signal..
	 */
	if (down_interruptible(&ihk_os_notifiers_lock)) {
		return -ERESTARTSYS;
	}

	if (data->ops->boot) {
		ret = data->ops->boot(data, data->priv, flag);
		if (ret == 0) {
			ret = ihk_ikc_master_init(data);
		}

		/* Call OS notifiers */
		if (ret == 0) {
			struct ihk_os_notifier *_ion;
			list_for_each_entry(_ion, &ihk_os_notifiers, nlist) {
				if (_ion->ops && _ion->ops->boot)
					_ion->ops->boot(index);
			}
		}
	}

	up(&ihk_os_notifiers_lock);
	return ret;
}

static void delete_kmsg_buf(struct ihk_kmsg_buf_container* cont) {
	if (!cont) {
		return;
	}

	__free_pages(virt_to_page(cont->kmsg_buf), cont->order);
	dkprintf("%s: __free_pages kmsg_buf\n", __FUNCTION__);
	
	list_del(&cont->list);
	kfree(cont);
	dkprintf("%s: kmsg_buf %p deleted\n", __FUNCTION__, cont);
}

static int release_kmsg_buf(struct ihk_kmsg_buf_container* cont) {
	unsigned long flags;

	if (atomic_read(&cont->count) == 0) {
		dkprintf("%s: Trying to unref kmsg_buf with count of zero\n", __FUNCTION__);
		return -EINVAL;
	}
	
    spin_lock_irqsave(&ihk_kmsg_bufs_lock, flags);
	if (atomic_dec_return(&cont->count) == 0) {
		delete_kmsg_buf(cont);
	}
    spin_unlock_irqrestore(&ihk_kmsg_bufs_lock, flags);
	return 0;
}

static int __ihk_os_status(struct ihk_host_linux_os_data *data);
static int __ihk_os_thaw(struct ihk_host_linux_os_data *data);

/** \brief Shutdown the kernel related to the OS file */
static int __ihk_os_shutdown(struct ihk_host_linux_os_data *data, int flag)
{
	int ret = -EINVAL;
	struct ihk_os_notifier *_ion;
	int index = ihk_host_os_get_index(data);
	enum ihk_os_status status = __ihk_os_status(data);

	switch (status) {
	case IHK_OS_STATUS_NOT_BOOTED:
		pr_err("%s: error: invalid os status: %d\n",
		       __func__, status);
		ret = -EINVAL;
		goto out;
	case IHK_OS_STATUS_SHUTDOWN:
		pr_err("%s: error: invalid os status: %d\n",
		       __func__, status);
		ret = -EBUSY;
		goto out;
	case IHK_OS_STATUS_FREEZING:
		/* wait 10 sec for frozen */
		pr_info("%s: waiting for frozen...\n", __func__);
		if (ihk_os_wait_for_status((ihk_os_t)data, IHK_OS_STATUS_FROZEN,
					   0, 100) != 0) {
			pr_info("%s: warning: wait for frozen timeouted\n",
			       __func__);
		}
	case IHK_OS_STATUS_FROZEN:
		pr_info("%s: trying to thaw...\n", __func__);
		ret = __ihk_os_thaw(data);
		if (ret) {
			pr_err("%s: error: __ihk_os_thaw: %d\n",
			       __func__, ret);
		}
		/* fall through */
	case IHK_OS_STATUS_BOOTING:
	case IHK_OS_STATUS_BOOTED:
	case IHK_OS_STATUS_READY:
		/* wait 20 sec for running */
		pr_info("%s: waiting for running...\n", __func__);
		if (ihk_os_wait_for_status((ihk_os_t)data, IHK_OS_STATUS_RUNNING,
					   0, 200) != 0) {
			pr_info("%s: warning: wait for running timeouted, "
			       "trying to shutdown with nmi...\n",
			       __func__);

			/* send nmi to force shutdown */
			ihk_os_send_nmi((ihk_os_t)data, 3);
			mdelay(200);
		}
		break;
	case IHK_OS_STATUS_RUNNING:
	case IHK_OS_STATUS_FAILED:
	case IHK_OS_STATUS_HUNGUP:
	default:
		break;
	}

	/* Call OS notifiers */
	if (down_interruptible(&ihk_os_notifiers_lock)) {
		return -ERESTARTSYS;
	}

	if (index != -1) {
		list_for_each_entry(_ion, &ihk_os_notifiers, nlist) {
			if (_ion->ops && _ion->ops->shutdown)
				_ion->ops->shutdown(index);
		}
	}
	up(&ihk_os_notifiers_lock);

	ikc_master_finalize(data);

	if (data->ops->shutdown) {
		ret = data->ops->shutdown(data, data->priv, flag);
		if (ret) {
			pr_err("%s: error: shutdown returned %d\n",
			       __func__, ret);
			goto out;
		}
	}

	/* Release kmsg_buf */
	if (data->kmsg_buf_container) {
		struct ihk_kmsg_buf_container *cont =
			data->kmsg_buf_container;
		data->kmsg_buf_container = NULL;
		ret = release_kmsg_buf(cont);
		if (ret) {
			dprintf("%s: error: release_kmsg_buf returned %d\n",
				__func__, ret);
			goto out;
		}
	}

	printk("IHK: OS shutdown OK\n"); 
	ret = 0;
 out:
	return ret;
}

/** \brief ioctl handler for a debug request to the OS file */
static int __ihk_os_ioctl_debug_request(struct ihk_host_linux_os_data *data,
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

/** \brief Memory allocating wrapper function for an OS kernel */
static int __ihk_os_allocate_mem(struct ihk_host_linux_os_data *data,
                                 unsigned long arg)
{
	struct ihk_resource resource;

	memset(&resource, 0, sizeof(resource));
	resource.mem_size = arg;

	return __ihk_os_alloc_resource(data, &resource);
}

/** \brief Processor allocating wrapper function for an OS kernel */
static int __ihk_os_allocate_cpu(struct ihk_host_linux_os_data *data,
                                 unsigned long arg)
{
	struct ihk_resource resource;

	memset(&resource, 0, sizeof(resource));
	resource.cpu_cores = arg;

	return __ihk_os_alloc_resource(data, &resource);
}

/** \brief Processor reserving wrapper function for an OS kernel */
static int __ihk_os_reserve_cpu(struct ihk_host_linux_os_data *data,
                                unsigned long ptr)
{
	struct ihk_resource *pres;
	int i, n;
	int *__user u = (int *)ptr;

	if (copy_from_user(&n, u, sizeof(int))) {
		return -EFAULT;
	}
	if (n < 0 || n > 512) {
		return -EINVAL;
	}

	pres = kmalloc(sizeof(struct ihk_resource) + sizeof(int) * n,
	               GFP_KERNEL);
	if (!pres) {
		return -ENOMEM;
	}
	memset(pres, 0, sizeof(struct ihk_resource) + sizeof(int) * n);
	pres->flags = IHK_RESOURCE_FLAG_CPU_SPECIFIED;
	pres->cpu_cores = n;

	u++;

	for (i = 0; i < n; i++) {
		if (copy_from_user(pres->cores + i, u++, sizeof(int))) {
			kfree(pres);
			return -EFAULT;
		}
	}

	n = __ihk_os_alloc_resource(data, pres);
	kfree(pres);

	return n;
}

/** \brief Memory reserving wrapper function for an OS kernel */
static int __ihk_os_reserve_mem(struct ihk_host_linux_os_data *data,
                                unsigned long ptr)
{
	unsigned long *__user u = (unsigned long *)ptr;
	unsigned long params[2];
	struct ihk_resource resource;
	
	if (copy_from_user(params, u, sizeof(unsigned long) * 2)) {
		return -EFAULT;
	}

	memset(&resource, 0, sizeof(resource));
	resource.flags = IHK_RESOURCE_FLAG_MEM_SPECIFIED;
	resource.mem_start = params[0];
	resource.mem_size = params[1];

	return __ihk_os_alloc_resource(data, &resource);
}

static int read_kmsg(struct ihk_kmsg_buf *kmsg_buf, char *buf, int shift)
{
	int len_bottom, len_top;
	unsigned long flags;

	if (!kmsg_buf) {
		return -EINVAL;
	}

	/* Inter-kernel lock for struct ihk_kmsg_buf */
	local_irq_save(flags);
	while(__sync_val_compare_and_swap(&kmsg_buf->lock, 0, 1) != 0) {
		cpu_relax();
	}
  
	if (kmsg_buf->head > kmsg_buf->tail) {
		len_bottom = strnlen(&kmsg_buf->str[kmsg_buf->head], kmsg_buf->len - kmsg_buf->head);
		len_top = kmsg_buf->tail;
	} else {
		len_bottom = kmsg_buf->tail - kmsg_buf->head;
		len_top = 0;
	}
	dkprintf("kmsg head=%d,tail=%d,len=%d,len_bottom=%d,len_top=%d\n", kmsg_buf->head, kmsg_buf->tail, kmsg_buf->len, len_bottom, len_top);

	/* Print the end of the buffer */
	if (len_bottom > 0) {
		memcpy(buf, &kmsg_buf->str[kmsg_buf->head], len_bottom);
	}

	/* Then the front of it */
	if (len_top > 0) {
		memcpy(buf + len_bottom, kmsg_buf->str, len_top);
	}

	if (shift) {
		kmsg_buf->head = kmsg_buf->tail; 
	}
	kmsg_buf->lock = 0;
	local_irq_restore(flags);

	return len_bottom + len_top;
}

/** \brief ioctl handler for reading the kernel message to the buffer */
static int __ihk_os_read_kmsg(struct ihk_host_linux_os_data *data,
                              char __user *_buf)
{
	int ret = 0;
	char *buf;

	if (!data->kmsg_buf_container) {
		return -EINVAL;
	}

	if (!data->kmsg_buf_container->kmsg_buf) {
		return -EINVAL;
	}

	buf = kmalloc(IHK_KMSG_SIZE, GFP_KERNEL);
	if (!buf) {
		ret = -ENOMEM;
		goto out;
	}

	ret = read_kmsg(data->kmsg_buf_container->kmsg_buf, buf, 0);
	if (ret < 0) {
		goto out;
	}

	if (copy_to_user(_buf, buf, ret)) {
		dprintf("error: copying string to user-space\n");
		ret = -EINVAL;
		goto out;
	}
 out:
	if (buf) {
		kfree(buf);
	}
	return ret;
}

/** \brief Set the kernel command-line parameter for the kernel
 *
 * This function accepts 1023 letters at most. */
static int __ihk_os_set_kargs(struct ihk_host_linux_os_data *data,
                              char __user *buf)
{
	char *kbuf;
	int error;

	kbuf = kmalloc(1024, GFP_KERNEL);
	if (!kbuf) {
		return -ENOMEM;
	}
	if (strncpy_from_user(kbuf, buf, 1024) < 0) {
		kfree(kbuf);
		return -EFAULT;
	}
	kbuf[1023] = 0;
	
	error = -EINVAL;
	if (data->ops->set_kargs) {
		error = data->ops->set_kargs(data, data->priv, kbuf);
	}

	kfree(kbuf);
	return error;
}

static void
setup_monitor(struct ihk_host_linux_os_data *data)
{
	unsigned long rpa;
	unsigned long pa;
	unsigned long size;
	unsigned long psize;

	if (data->monitor)
		return;

	if (__ihk_os_get_special_addr(data, IHK_SPADDR_MONITOR, &rpa, &size)) {
		dprintf("get_special_addr: failed.\n");
		return;
	}

	psize = ((size + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE;
	pa = __ihk_os_map_memory(data, rpa, psize);

#ifdef CONFIG_MIC
	if ((long)pa <= 0) {
		return;
	}

	data->monitor = ioremap_nocache(pa, psize);
#else
	data->monitor = ihk_device_map_virtual(data->dev_data, pa, psize,
	                                       NULL, 0);
#endif
	data->monitor_pa = pa;
	data->monitor_len = size;
}

static void
setup_rusage(struct ihk_host_linux_os_data *data)
{
	unsigned long rpa;
	unsigned long pa;
	unsigned long size;
	unsigned long psize;

	if (data->rusage)
		return;

	if (__ihk_os_get_special_addr(data, IHK_SPADDR_RUSAGE, &rpa, &size)) {
		dprintf("get_special_addr: failed.\n");
		return;
	}

	psize = ((size + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE;
	pa = __ihk_os_map_memory(data, rpa, psize);

#ifdef CONFIG_MIC
	if ((long)pa <= 0) {
		return;
	}

	data->rusage = ioremap_nocache(pa, psize);
#else
	data->rusage = ihk_device_map_virtual(data->dev_data, pa, psize,
	                                       NULL, 0);
#endif
	data->rusage_pa = pa;
	data->rusage_len = size;
}

static int detect_hungup(struct ihk_host_linux_os_data *data)
{
	int ret;
	int n;
	int i;

	ret = __ihk_os_query_status(data);
	dkprintf("%s: __ihk_os_query_status returned %d", __FUNCTION__, ret);

	/* Guard objects referenced here
	   (1) LWK sets boot_param->status to 1 (__ihk_os_query_status returns IHK_OS_STATUS_BOOTED) in arch_init()
	   (2) LWK initializes IHK_SPADDR_MONITOR
	   (3) LWK sets boot_param->status to 2 (__ihk_os_query_status returns IHK_OS_STATUS_READY) in arch_ready()
	   (4) LWK sets boot_param->status to 3 (__ihk_os_query_status returns IHK_OS_STATUS_RUNNING) in done_init() */
	if (ret == IHK_OS_STATUS_HUNGUP) {
		goto out;
	} else if (ret != IHK_OS_STATUS_READY && ret != IHK_OS_STATUS_RUNNING) {
		ret = -EAGAIN;
		goto out;
	}

	setup_monitor(data);
	if (data->monitor == NULL) {
		ret = -ENOSYS;
		goto out;
	}

	n = data->monitor->num_processors;
	for (i = 0; i < n; i++) {
		dkprintf("%s: data->monitor->cpu[%d].status=%d\n", __FUNCTION__, i, data->monitor->cpu[i].status);
		if(data->monitor->cpu[i].status == IHK_OS_MONITOR_PANIC){
			dkprintf("%s: cpu[%d].status==%d\n", __FUNCTION__, i, data->monitor->cpu[i].status);
			ret = IHK_OS_STATUS_FAILED;
			goto out;
		}

		if(data->monitor->cpu[i].status == IHK_OS_MONITOR_KERNEL){
			dkprintf("%s: cpu[%d].status==KERNEL,c=%ld,o=%ld\n", __FUNCTION__, i, data->monitor->cpu[i].counter, data->monitor->cpu[i].ocounter);
			if(data->monitor->cpu[i].counter ==
			   data->monitor->cpu[i].ocounter) {
				dkprintf("%s: HUNGUP detected\n", __FUNCTION__);
				ret = IHK_OS_STATUS_HUNGUP;
				__ihk_os_notify_hungup(data);
				ihk_os_eventfd((ihk_os_t)data, IHK_OS_EVENTFD_TYPE_STATUS);
			}
		}
		data->monitor->cpu[i].ocounter = data->monitor->cpu[i].counter;
	}

 out:
	dkprintf("%s: returning %d\n", __FUNCTION__, ret);
	return ret;
}

static int __ihk_os_status(struct ihk_host_linux_os_data *data,
		char __user *buf)
{
	int n;
	int i;
	int status;
	int freezing;

	status = __ihk_os_query_status(data);

	/* (1) LWK sets boot_param->status to 1 (__ihk_os_query_status returns IHK_OS_STATUS_BOOTED) in arch_init()
	   (2) LWK initializes IHK_SPADDR_MONITOR
	   (3) LWK sets boot_param->status to 2 (__ihk_os_query_status returns IHK_OS_STATUS_READY) in arch_ready()
	   (4) LWK sets boot_param->status to 3 (__ihk_os_query_status returns IHK_OS_STATUS_RUNNING) in done_init() */
	if (status != IHK_OS_STATUS_READY && status != IHK_OS_STATUS_RUNNING)
		return status;

	setup_monitor(data);
	if (data->monitor == NULL) {
		return -ENOSYS;
	}

	n = data->monitor->num_processors;
	for (i = 0; i < n; i++) {
		if(data->monitor->cpu[i].status == IHK_OS_MONITOR_PANIC){
			dkprintf("%s: cpu[%d].status==%d\n", __FUNCTION__, i, data->monitor->cpu[i].status);
			return IHK_OS_STATUS_FAILED;
		}

	}

	freezing = data->monitor->cpu[0].status;
	if (freezing == IHK_OS_MONITOR_KERNEL_FREEZING)
		return IHK_OS_STATUS_FREEZING;
	for (i = 1; i < n; i++) {
		switch (data->monitor->cpu[i].status) {
		    case IHK_OS_MONITOR_KERNEL_FREEZING:
			return IHK_OS_STATUS_FREEZING;
			break;
		    case IHK_OS_MONITOR_KERNEL_FROZEN:
			if (freezing != IHK_OS_MONITOR_KERNEL_FROZEN) {
				return IHK_OS_STATUS_FREEZING;
			}
			break;
		    default:
			if (freezing == IHK_OS_MONITOR_KERNEL_FROZEN) {
				return IHK_OS_STATUS_FREEZING;
			}
			break;
		}
	}
	if (freezing == IHK_OS_MONITOR_KERNEL_FROZEN)
		return IHK_OS_STATUS_FROZEN;

	return status;
}

/** \brief Clear the kernel message buffer. */
static int __ihk_os_clear_kmsg(struct ihk_host_linux_os_data *data)
{
	struct ihk_kmsg_buf *kmsg_buf;
	unsigned long flags;

	if (!data->kmsg_buf_container) {
		return -EINVAL;
	}
	
	if (!data->kmsg_buf_container->kmsg_buf) {
		return -EINVAL;
	}

	kmsg_buf = data->kmsg_buf_container->kmsg_buf;

	local_irq_save(flags);
	while(__sync_val_compare_and_swap(&kmsg_buf->lock, 0, 1) != 0) {
		cpu_relax();
	}

	memset(kmsg_buf->str, 0, kmsg_buf->len);
	kmsg_buf->head = 0;
	kmsg_buf->tail = 0;

	kmsg_buf->lock = 0;
	local_irq_restore(flags);

	return 0;
}

static int __ihk_os_register_event(struct ihk_host_linux_os_data *os, void __user *_desc)
{
	struct ihk_event *ep;
	struct ihk_os_ioctl_eventfd_desc desc;
	struct eventfd_ctx *event;
	struct file *filp;
	unsigned long flags;

	if (copy_from_user(&desc, _desc, sizeof(desc))) {
		return -EFAULT;
	}

	filp = eventfd_fget(desc.fd);
	if (IS_ERR(filp)) {
		return PTR_ERR(filp);
	}
	event = eventfd_ctx_fileget(filp);
	if (IS_ERR(event)) {
		return PTR_ERR(event);
	}
	ep = kzalloc(sizeof(struct ihk_event), GFP_KERNEL);
	ep->event = event;
	ep->type = desc.type;
	spin_lock_irqsave(&os->event_list_lock, flags);
	list_add_tail(&ep->list, &os->event_list);
	spin_unlock_irqrestore(&os->event_list_lock, flags);
	return 0;
}

void ihk_os_eventfd(ihk_os_t data, int type)
{
	unsigned long flags;
	struct ihk_event *ep;
	struct ihk_host_linux_os_data *os = (struct ihk_host_linux_os_data *)data;

	spin_lock_irqsave(&os->event_list_lock, flags);
	list_for_each_entry(ep, &os->event_list, list) {
		if (ep->type == type) {
			dkprintf("%s: calling eventfd_signal,ep->type=%d,type=%d\n", __FUNCTION__, ep->type, type);		
			eventfd_signal(ep->event, 1);
		}
	}
	spin_unlock_irqrestore(&os->event_list_lock, flags);
}

static int __ihk_os_dump(struct ihk_host_linux_os_data *data, void __user *uargsp) {
	dumpargs_t args;
	int error = -EFAULT;

	if (copy_from_user(&args, uargsp, sizeof(args))) {
		return -EFAULT;
	}

	if (data->ops->dump) {
		error = (*data->ops->dump)(data, data->priv, &args);
	}

	if (copy_to_user(uargsp, &args, sizeof(args))) {
		return -EFAULT;
	}
	return error;
}

static int __ihk_os_freeze(struct ihk_host_linux_os_data *data)
{
	int error = 0;

	if (data->ops->freeze) {
		error = (*data->ops->freeze)(data, data->priv);
	}

	return error;
}

static int __ihk_os_thaw(struct ihk_host_linux_os_data *data)
{
	int error = 0;

	if (data->ops->thaw) {
		error = (*data->ops->thaw)(data, data->priv);
	}

	return error;
}

static int __ihk_os_get_usage(struct ihk_host_linux_os_data *data, unsigned long arg)
{
	struct ihk_os_monitor *__user buf;

	setup_monitor(data);
	if (data->monitor == NULL) {
		return -ENOSYS;
	}

	buf = (struct ihk_os_monitor *__user)arg;
	if (copy_to_user(buf, data->monitor, sizeof(struct ihk_os_monitor))) {
		return -EFAULT;
	}

	return 0;
}

static int __ihk_os_get_cpu_usage(struct ihk_host_linux_os_data *data, unsigned long arg)
{
	struct ihk_os_cpu_monitor *__user buf;
	int size;

	setup_monitor(data);
	if (data->monitor == NULL) {
		return -ENOSYS;
	}

	buf = (struct ihk_os_cpu_monitor *__user)arg;
	size = sizeof(struct ihk_os_cpu_monitor) *
	       data->monitor->num_processors;
	if (copy_to_user(buf, data->monitor->cpu, size)) {
		return -EFAULT;
	}

	return 0;
}

static int __ihk_os_read_kaddr(struct ihk_host_linux_os_data *data, void __user *arg)
{
	struct ihk_os_read_kaddr_desc desc;
	unsigned long phys;

	if (copy_from_user(&desc, arg, sizeof(desc))) {
		return -EFAULT;
	}

	if (data->ops->vtop(data, data->priv, desc.kaddr, &phys) != 0) {
		return -EFAULT;
	}

	if (copy_to_user(desc.ubuf, phys_to_virt(phys), desc.len)) {
		return -EFAULT;
	}

	return 0;
}

/** \brief Handles ioctl calls with the additional request number */
static long __ihk_os_ioctl_call_aux(struct ihk_host_linux_os_data *os,
                                    unsigned int request, unsigned long arg,
                                    struct file *file)
{
	struct ihk_os_user_call *c;
	int i;
	
	/* XXX: Very awkward iteration, and no lock! */
	list_for_each_entry(c, &os->aux_call_list, list) {
		for (i = 0; i < c->num_handlers; i++) {
			if (c->handlers[i].request == request) {
				return c->handlers[i].func(os, request,
				                           c->handlers[i].priv,
				                           arg, file);
			}
		}
	}

	return -ENOSYS;
}

static int __ihk_os_ioctl_perm(unsigned int request)
{
	int ret = 0;
	kuid_t euid;

	switch (request) {
	case IHK_OS_QUERY_STATUS:
	case IHK_OS_GET_NUM_NUMA_NODES:
	case IHK_OS_QUERY_FREE_MEM:
	case IHK_OS_ALLOC_CPU:
	case IHK_OS_ALLOC_MEM:
	case IHK_OS_RESERVE_CPU:
	case IHK_OS_RESERVE_MEM:
	case IHK_OS_READ_KMSG:
	case IHK_OS_CLEAR_KMSG:
	case IHK_OS_QUERY_CPU:
	case IHK_OS_QUERY_MEM:
	case IHK_OS_GET_IKC_MAP:
	case IHK_OS_GET_BUILDID:
	case IHK_OS_STATUS:
	case IHK_OS_GET_USAGE:
	case IHK_OS_GET_CPU_USAGE:
	case IHK_OS_REGISTER_EVENT:
	case IHK_OS_GET_NUM_CPUS:
	case IHK_OS_READ_KADDR:
		break;
	default:
		if (request >= IHK_OS_DEBUG_START && 
			request <= IHK_OS_DEBUG_END) {
			break;
		}
		else if (request >= IHK_OS_AUX_CALL_START &&
		           request <= IHK_OS_AUX_CALL_END) {
			break;
		}

		euid = current_euid();
		dprintf("%s: request=0x%x, euid=%u\n",
			__FUNCTION__, request, euid.val);
		if (euid.val) {
			ret = -EPERM;
		}
		break;
	}
	dprintf("%s: request=0x%x, ret=%d\n", __FUNCTION__, request, ret);

	return ret;
}

/** \brief ioctl handling for a OS file */
static long ihk_host_os_ioctl(struct file *file, unsigned int request,
                              unsigned long arg)
{
	int ret = -EINVAL;
	struct ihk_host_linux_os_data *data;
	struct ihk_file *ifile;
	
	ifile = file->private_data;
	data = ifile->osdata;

/*	dprintf("IHK: ioctl request = %x, arg = %lx\n", request, arg); */

	ret = __ihk_os_ioctl_perm(request);
	if (ret) {
		dprintf("%s: __ihk_os_ioctl_perm(0x%x) error(%d)\n",
			__FUNCTION__, request, ret);
		return ret;
	}

	switch (request) {
	case IHK_OS_LOAD:
		ret = __ihk_os_ioctl_load(data, (char * __user)arg);
		break;

	case IHK_OS_BOOT:
		ret = __ihk_os_boot(data, arg);
		break;

	case IHK_OS_SHUTDOWN:
		ret = __ihk_os_shutdown(data, arg);
		break;

	case IHK_OS_ALLOC_CPU:
		ret = __ihk_os_allocate_cpu(data, arg);
		break;

	case IHK_OS_ALLOC_MEM:
		ret = __ihk_os_allocate_mem(data, arg);
		break;

	case IHK_OS_RESERVE_CPU:
		ret = __ihk_os_reserve_cpu(data, arg);
		break;

	case IHK_OS_RESERVE_MEM:
		ret = __ihk_os_reserve_mem(data, arg);
		break;

	case IHK_OS_ASSIGN_CPU:
		ret = __ihk_os_assign_cpu(data, arg);
		break;

	case IHK_OS_RELEASE_CPU:
		ret = __ihk_os_release_cpu(data, arg);
		break;

	case IHK_OS_SET_IKC_MAP:
		ret = __ihk_os_set_ikc_map(data, arg);
		break;

	case IHK_OS_GET_IKC_MAP:
		ret = __ihk_os_get_ikc_map(data, arg);
		break;

	case IHK_OS_GET_BUILDID:
		ret = __ihk_os_get_buildid(data, arg);
		break;

	case IHK_OS_GET_NUM_CPUS:
		ret = __ihk_os_get_num_cpus(data);
		break;

	case IHK_OS_QUERY_CPU:
		ret = __ihk_os_query_cpu(data, arg);
		break;

	case IHK_OS_ASSIGN_MEM:
		ret = __ihk_os_assign_mem(data, arg);
		break;

	case IHK_OS_RELEASE_MEM:
		ret = __ihk_os_release_mem(data, arg);
		break;

	case IHK_OS_QUERY_MEM:
		ret = __ihk_os_query_mem(data, arg);
		break;

	case IHK_OS_QUERY_STATUS:
		ret = __ihk_os_query_status(data);
		break;

	case IHK_OS_DETECT_HUNGUP:
		ret = detect_hungup(data);
		break;

	case IHK_OS_NOTIFY_HUNGUP:
		__ihk_os_notify_hungup(data);
		ret = 0;
		break;

	case IHK_OS_GET_NUM_NUMA_NODES:
		ret = __ihk_os_get_num_numa_nodes(data);
		break;

	case IHK_OS_QUERY_FREE_MEM:
		ret = __ihk_os_query_free_mem(data);
		break;

	case IHK_OS_SET_KARGS:
		ret = __ihk_os_set_kargs(data, (char * __user)arg);
		break;

	case IHK_OS_READ_KMSG:
		ret = __ihk_os_read_kmsg(data, (char * __user)arg);
		break;

	case IHK_OS_STATUS:
		ret = __ihk_os_status(data, (char * __user)arg);
		dkprintf("%s: __ihk_os_status returned %d\n", __FUNCTION__, ret);
		break;

	case IHK_OS_CLEAR_KMSG:
		ret = __ihk_os_clear_kmsg(data);
		break;

	case IHK_OS_DUMP:
		ret = __ihk_os_dump(data, (char __user *)arg);
		break;

	case IHK_OS_REGISTER_EVENT:
		ret = __ihk_os_register_event(data, (void __user *)arg);
		break;

	case IHK_OS_EVENTFD:
		ihk_os_eventfd(data, (int)arg);
		ret = 0;
		break;

	case IHK_OS_FREEZE:
		ret = __ihk_os_freeze(data);
		dkprintf("__ihk_os_freeze(ret=%d)\n",ret);
		break;

	case IHK_OS_THAW:
		ret = __ihk_os_thaw(data);
		dkprintf("__ihk_os_thaw  (ret=%d)\n",ret);
		break;

	case IHK_OS_GET_USAGE:
		ret = __ihk_os_get_usage(data, arg);
		dkprintf("__ihk_os_get_usage  (ret=%d)\n",ret);
		break;

	case IHK_OS_GET_CPU_USAGE:
		ret = __ihk_os_get_cpu_usage(data, arg);
		dkprintf("__ihk_os_get_cpu_usage  (ret=%d)\n",ret);
		break;

	case IHK_OS_READ_KADDR:
		ret = __ihk_os_read_kaddr(data, (void __user *)arg);
		break;

	default:
		if (request >= IHK_OS_DEBUG_START && 
		    request <= IHK_OS_DEBUG_END) {
			ret = __ihk_os_ioctl_debug_request(data,
			                                   request, arg);
		} else if (request >= IHK_OS_AUX_CALL_START &&
		           request <= IHK_OS_AUX_CALL_END) {
			ret = __ihk_os_ioctl_call_aux(data, request, arg, file);
		}
		break;
	}

	return ret;
}

/** \brief write handling for a OS file */
static long ihk_host_os_write(struct file *file, const char __user *buf,
                              size_t size, loff_t *off)
{
	int r;
	struct ihk_file *ifile = file->private_data;
	struct ihk_host_linux_os_data *data = ifile->osdata;
	char *ubuf;

	if (size > PAGE_SIZE * 16) {
		return -E2BIG;
	}
	ubuf = kmalloc(size, GFP_KERNEL);
	if (!ubuf) {
		return -ENOMEM;
	}

	r = copy_from_user(ubuf, buf, size);
	if (r > 0) {
		kfree(ubuf);
		return -EFAULT;
	}

	r = __ihk_os_load_memory(data, ubuf, size, *off);
	kfree(ubuf);

	if (r == 0) {
		*off += size;
		return size;
	} else {
		return r;
	}
}

static struct file_operations mcos_cdev_ops = {
	.open = ihk_host_os_open,
	.write = ihk_host_os_write,
	.unlocked_ioctl = ihk_host_os_ioctl,
	.release = ihk_host_os_release,
};

/*
 * Device character device file operations.
 */

static int __ihk_device_get_kmsg_buf(struct file *file, void __user * _desc)
{
	int found = 0;
	struct ihk_kmsg_buf_container *cont;
	struct ihk_device_get_kmsg_buf_desc desc;
	unsigned long flags;

    if (copy_from_user(&desc, _desc, sizeof(desc))) {
        return -EFAULT;
    }

	dkprintf("%s: os_index=%d\n", __FUNCTION__, desc.os_index);

	/* Get the latest kmsg_buf */
	spin_lock_irqsave(&ihk_kmsg_bufs_lock, flags);
	list_for_each_entry_reverse(cont, &ihk_kmsg_bufs, list) {
		if (cont->os_index == desc.os_index) {
			dkprintf("%s: got kmsg_buf %p\n", __FUNCTION__, cont);
			atomic_inc(&cont->count); /* ihkmond is referring to it */
			found = 1;
			break;
		}
	}
	spin_unlock_irqrestore(&ihk_kmsg_bufs_lock, flags);
	
	if (!found) {
		return -EINVAL;
	}

	desc.handle = cont;
	if (copy_to_user(_desc, &desc, sizeof(desc))) {
		return -EFAULT;
	}

	return 0;
}

/** \brief ioctl handler for reading the kernel message to the buffer */
static int __ihk_device_read_kmsg_buf(struct file *file, void __user *_desc)
{
	int ret = 0;
	char *buf;
	struct ihk_kmsg_buf_container *cont;
	struct ihk_device_read_kmsg_buf_desc desc;

	if (copy_from_user(&desc, _desc, sizeof(desc))) {
        return -EFAULT;
    }

	buf = kmalloc(IHK_KMSG_SIZE, GFP_KERNEL);
	if (!buf) {
		ret = -ENOMEM;
		goto out;
	}

	cont = (struct ihk_kmsg_buf_container *)desc.handle;
	ret = read_kmsg(cont->kmsg_buf, buf, desc.shift);
	if (ret < 0) {
		goto out;
	}

	if (copy_to_user(desc.buf, buf, ret)) {
		dprintf("error: copying string to user-space\n");
		ret = -EINVAL;
		goto out;
	}
 out:
	if (buf) {
		kfree(buf);
	}
	return ret;
}

static int __ihk_device_release_kmsg_buf(struct file *file, unsigned long arg)
{
	return release_kmsg_buf((struct ihk_kmsg_buf_container *)arg);
}

/** \brief open handler for a device file */
static int ihk_host_device_open(struct inode *inode, struct file *file)
{
	int idx, ret;
	struct ihk_host_linux_device_data *data;

	idx = inode->i_rdev - mcd_dev_num;
	if (idx < 0 || idx > dev_max_minor) {
		return -EINVAL;
	}

	data = dev_data[idx];
	if (!data || data == DEV_DATA_INVALID) {
		return -EINVAL;
	}
	if (data->flag & IHK_DEVICE_FLAG_SHARABLE) {
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

/** \brief release handler for a device file */
static int ihk_host_device_release(struct inode *inode, struct file *file)
{
	int ret = 0;
	struct ihk_host_linux_device_data *data;

	data = file->private_data;

	if (data->ops->close) {
		data->ops->close(data, data->priv, file);
	}

	atomic_dec(&data->refcount);
	return ret;
}

/** \brief release handler for a device file */
static int __ihk_device_ioctl_debug_request(struct ihk_host_linux_device_data *
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

/** \brief Initialize a newly created OS structure */
static int __ihk_device_create_os_init(struct ihk_host_linux_device_data *data,
                                       struct ihk_host_linux_os_data **os_ptr,
                                       unsigned long arg)
{
	struct ihk_host_linux_os_data *os = NULL;
	struct ihk_register_os_data drv_data;
	int ret = 0;

	os = kzalloc(sizeof(*os), GFP_KERNEL);
	if (!os) {
		printk("ihk: kzalloc failed\n");
		ret = -ENOMEM;
		goto ERR;
	}
	spin_lock_init(&os->lock);
	mutex_init(&os->kmsg_mutex);
	atomic_set(&os->refcount, 0);

	memset(&drv_data, 0, sizeof(drv_data));

	spin_lock_init(&os->listener_lock);
	spin_lock_init(&os->wait_lock);
	spin_lock_init(&os->event_list_lock);
	INIT_LIST_HEAD(&os->ikc_channels);

	os->regular_channels = kzalloc(sizeof(*os->regular_channels) *
			num_possible_cpus(), GFP_KERNEL);
	if (!os->regular_channels) {
		ret = -ENOMEM;
		printk("ihk: error allocating channels\n");
		goto ERR;
	}

	INIT_LIST_HEAD(&os->wait_list);
	INIT_LIST_HEAD(&os->aux_call_list);
	INIT_LIST_HEAD(&os->event_list);

	if (data->ops->create_os && 
	    (ret = data->ops->create_os(data, data->priv, arg, 
	                                os, &drv_data))) {
		printk("ihk: create_os failed (%d)\n", ret);
		goto ERR;
	}

	os->name = drv_data.name;
	os->flag = IHK_OS_FLAG_SHARABLE;
	os->ops = drv_data.ops;
	os->priv = drv_data.priv;
	os->dev_data = data;

	cdev_init(&os->cdev, &mcos_cdev_ops);
	
	*os_ptr = os;

	return 0;

ERR:
	if (os) {
		kfree(os->regular_channels);
		kfree(os);
	}
	return ret;
}

static int __ihk_device_get_buildid(struct ihk_host_linux_device_data *data,
                                  unsigned long arg)
{
	char buildid[] = BUILDID;
	if (copy_to_user((void*)arg, buildid, sizeof(buildid))) {
		return -EFAULT;
	}
	return 0;
}

static int __ihk_device_destroy_os(struct ihk_host_linux_device_data *data,
				   struct ihk_host_linux_os_data *os);

/** \brief Create a OS file in the kernel
 *
 * @return minor number */
static int __ihk_device_create_os(struct ihk_host_linux_device_data *data,
                                  unsigned long arg)
{
	int i, minor, ret;
	unsigned long flags;
	struct ihk_host_linux_os_data *os = NULL;
	int kmsg_buf_size;
	unsigned int kmsg_buf_order;
	struct page *kmsg_buf_pages;
	struct ihk_kmsg_buf_container *cont = NULL;
	struct ihk_kmsg_buf *kmsg_buf;
	int nbufs = 0;

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
			printk("ihk: os_max_minor exceeds.\n");
			return -ENOMEM;
		}
		os_max_minor++;
	}

	minor = i;
	os_data[minor] = OS_DATA_INVALID;

	spin_unlock_irqrestore(&os_data_lock, flags);

	if ((ret = __ihk_device_create_os_init(data, &os, arg)) != 0) {
		os_data[minor] = NULL;
		return ret;
	}

	/* Allocate kmsg_buf. Note that IHK-Core owns the buf. */
	kmsg_buf_size = (sizeof(struct ihk_kmsg_buf) + PAGE_SIZE - 1) & PAGE_MASK;
	kmsg_buf_order = 0;
	while (((size_t)PAGE_SIZE << kmsg_buf_order) < kmsg_buf_size)
		++kmsg_buf_order;

	kmsg_buf_pages = alloc_pages(GFP_KERNEL | __GFP_ZERO, kmsg_buf_order);
	if (!kmsg_buf_pages) {
		pr_info("IHK: Cannot allocate kmsg buffer\n");
		ret = -ENOMEM;
		goto error;
	}

	/* Initialize kmsg_buf */
	kmsg_buf = (struct ihk_kmsg_buf *)pfn_to_kaddr(page_to_pfn(kmsg_buf_pages));
	kmsg_buf->tail = 0;
	kmsg_buf->len = sizeof(kmsg_buf->str);
	kmsg_buf->head = 0;
	kmsg_buf->lock = 0;
	memset(kmsg_buf->str, 0, sizeof(kmsg_buf->str));
	dkprintf("%s: kmsg_buf=%p\n", __FUNCTION__, kmsg_buf);

	/* Release stray kmsg_bufs */
	spin_lock_irqsave(&ihk_kmsg_bufs_lock, flags);
	list_for_each_entry(cont, &ihk_kmsg_bufs, list) {
		nbufs++;
	}
	dkprintf("%s: number of kmsg_buf=%d\n", __FUNCTION__, nbufs);
	for (i = 0; i < nbufs - (IHK_MAX_NUM_KMSG_BUFS - 1); i++) {
		cont = list_first_entry(&ihk_kmsg_bufs, struct ihk_kmsg_buf_container, list);
		delete_kmsg_buf(cont);
		ekprintf("%s: Warning: stray kmsg_buf %p freed\n", __FUNCTION__, cont);
	}
	spin_unlock_irqrestore(&ihk_kmsg_bufs_lock, flags);

	/* Insert it into the list */
	cont = kmalloc(sizeof(struct ihk_kmsg_buf_container), GFP_KERNEL);
	if (!cont) {
		pr_info("IHK: Cannot allocate kmsg buffer container\n");
		__free_pages(kmsg_buf_pages, kmsg_buf_order);
		ret = -ENOMEM;
		goto error;
	}
	cont->os_index = minor;
	cont->kmsg_buf = kmsg_buf;
	atomic_set(&cont->count, 0);
	cont->order = kmsg_buf_order;
	spin_lock_irqsave(&ihk_kmsg_bufs_lock, flags);
	list_add_tail(&cont->list, &ihk_kmsg_bufs);
	spin_unlock_irqrestore(&ihk_kmsg_bufs_lock, flags);
	dkprintf("%s: kmsg_buf %p added\n", __FUNCTION__, cont);


	os->cdev.owner = THIS_MODULE;
	os->dev_num = mcos_dev_num + minor;

	if (cdev_add(&os->cdev, os->dev_num, 1) < 0) {
		printk("ihk: cdev_add failed (%d)\n", ret);
		ret = -ENOMEM;
		goto error;
	}

	/* set os_data[minor] before creating device to avoid creating
	 * the device before it's useable
	 */
	os_data[minor] = os;

	os->lindev = device_create(mcos_class, NULL, os->dev_num, NULL,
			OS_DEV_NAME "%d", minor);
	if (IS_ERR(os->lindev)) {
		printk("ihk: device_create failed.\n");
		ret = -ENOMEM;
		goto error;
	}

	return minor;

error:
	delete_kmsg_buf(cont);
	__ihk_device_destroy_os(data, os);
	return ret;
}

/** \brief Destroy an OS structure, and also the corresponding device file */
static int __ihk_device_destroy_os(struct ihk_host_linux_device_data *data,
                                   struct ihk_host_linux_os_data *os)
{
	int ret = 0;

	dkprintf("__ihk_device_destroy_os (%p, %p)\n", data, os);
	if (!os || os == OS_DATA_INVALID || !data || data == DEV_DATA_INVALID
	    || os->dev_data != data) {
		dkprintf("%s: pointer invalid\n", __FUNCTION__);
		return -EINVAL;
	}
	
	if (atomic_read(&os->refcount) > 0) {
		dkprintf("%s: refcount != 0\n", __FUNCTION__);
		return -EBUSY;
	}

	__ihk_os_shutdown(os, FLAG_IHK_OS_SHUTDOWN_FORCE);

	if (data->ops->destroy_os) {
		ret = data->ops->destroy_os(data, data->priv, os, os->priv);
	}

	if (ret != 0) {
		return -EINVAL;
	}

	while (!list_empty(&os->event_list)) {
		struct ihk_event *ep;
		ep = list_first_entry(&os->event_list, struct ihk_event, list);
		eventfd_ctx_put(ep->event);
		list_del(&ep->list);
		kfree(ep);
	}

	os_data[os->minor] = NULL;

	cdev_del(&os->cdev);
	device_destroy(mcos_class, os->dev_num);

	if (os->regular_channels)
		kfree(os->regular_channels);
	kfree(os);

	return 0;
}

/** \brief Destroy all the OS kernel stuffs of the specified device */
static int __destroy_all_os(struct ihk_host_linux_device_data *data)
{
	unsigned long flags;
	int i, r;
	struct ihk_host_linux_os_data *os;

	/*
	 * We assume that the newer OS is allocated in the higher index 
	 */
	spin_lock_irqsave(&os_data_lock, flags);
	for (i = 0; i < os_max_minor; i++) {
		if (os_data[i] && os_data[i] != (void *)-1 &&
		    os_data[i]->dev_data == data) {
			os = os_data[i];
			os_data[i] = NULL;
			spin_unlock_irqrestore(&os_data_lock, flags);

			r = __ihk_device_destroy_os(data, os);
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

/** \brief Reserve CPU cores */
static int __ihk_device_reserve_cpu(struct ihk_host_linux_device_data *data,
		unsigned long arg)
{
	if (!data->ops || !data->ops->reserve_cpu)
		return -1;

	return data->ops->reserve_cpu(data, arg);
}

/** \brief Release CPU cores */
static int __ihk_device_release_cpu(struct ihk_host_linux_device_data *data,
		unsigned long arg)
{
	if (!data->ops || !data->ops->release_cpu)
		return -1;

	return data->ops->release_cpu(data, arg);
}

/** \brief Reserve memory */
static int __ihk_device_reserve_mem(struct ihk_host_linux_device_data *data,
		unsigned long arg)
{
	if (!data->ops || !data->ops->reserve_mem)
		return -1;

	return data->ops->reserve_mem(data, arg);
}

/** \brief Release memory */
static int __ihk_device_release_mem(struct ihk_host_linux_device_data *data,
		unsigned long arg)
{
	if (!data->ops || !data->ops->release_mem)
		return -1;

	return data->ops->release_mem(data, arg);
}

/** \brief Release memory */
static int __ihk_device_release_mem_partially(struct ihk_host_linux_device_data *data,
					      unsigned long arg)
{
	if (!data->ops || !data->ops->release_mem_partially)
		return -1;

	return data->ops->release_mem_partially(data, arg);
}

/** \brief Query number of CPU cores */
static int __ihk_device_get_num_cpus(struct ihk_host_linux_device_data *data)
{
	if (!data->ops || !data->ops->get_num_cpus)
		return -1;

	return data->ops->get_num_cpus(data);
}

/** \brief Query CPU cores */
static int __ihk_device_query_cpu(struct ihk_host_linux_device_data *data,
		unsigned long arg)
{
	if (!data->ops || !data->ops->query_cpu)
		return -1;

	return data->ops->query_cpu(data, arg);
}

/** \brief Query memory */
static int __ihk_device_query_mem(struct ihk_host_linux_device_data *data,
		unsigned long arg)
{
	if (!data->ops || !data->ops->query_mem)
		return -1;

	return data->ops->query_mem(data, arg);
}

/** \brief ioctl handler for the device file */
static long ihk_host_device_ioctl(struct file *file, unsigned int request,
                                  unsigned long arg)
{
	int ret = -EINVAL;
	struct ihk_host_linux_device_data *data;
	
	data = file->private_data;

	switch (request) {
	case IHK_DEVICE_GET_BUILDID:
		ret = __ihk_device_get_buildid(data, arg);
		break;

	case IHK_DEVICE_CREATE_OS:
		ret = __ihk_device_create_os(data, arg);
		break;
	
	case IHK_DEVICE_DESTROY_OS:
		if (arg > OS_MAX_MINOR || !os_data[arg]) {
			printk("IHK: error: no OS exists with id %lu\n", arg);
			return ret;
		}

		ret = __ihk_device_destroy_os(data, os_data[arg]);
		break;

	case IHK_DEVICE_RESERVE_CPU:
		ret = __ihk_device_reserve_cpu(data, arg);
		break;

	case IHK_DEVICE_RELEASE_CPU:
		ret = __ihk_device_release_cpu(data, arg);
		break;

	case IHK_DEVICE_RESERVE_MEM:
		ret = __ihk_device_reserve_mem(data, arg);
		break;

	case IHK_DEVICE_RELEASE_MEM:
		ret = __ihk_device_release_mem(data, arg);
		break;

	case IHK_DEVICE_RELEASE_MEM_PARTIALLY:
		ret = __ihk_device_release_mem_partially(data, arg);
		break;

	case IHK_DEVICE_GET_NUM_CPUS:
		ret = __ihk_device_get_num_cpus(data);
		break;

	case IHK_DEVICE_QUERY_CPU:
		ret = __ihk_device_query_cpu(data, arg);
		break;

	case IHK_DEVICE_QUERY_MEM:
		ret = __ihk_device_query_mem(data, arg);
		break;

	case IHK_DEVICE_GET_KMSG_BUF:
		ret = __ihk_device_get_kmsg_buf(file, (void __user *)arg);
		break;

	case IHK_DEVICE_READ_KMSG_BUF:
		ret = __ihk_device_read_kmsg_buf(file, (void __user *)arg);
		break;

	case IHK_DEVICE_RELEASE_KMSG_BUF:
		ret = __ihk_device_release_kmsg_buf(file, arg);
		break;

	default:
		if (request >= IHK_DEVICE_DEBUG_START && 
		    request <= IHK_DEVICE_DEBUG_END) {
			ret = __ihk_device_ioctl_debug_request(data,
			                                       request, arg);
		}
		break;
	}

	return ret;
}

/** \brief read handler for the device file */
static long ihk_host_device_read(struct file *file, char __user *buf,
                                 size_t size, loff_t *off)
{
	unsigned long pa;
	void *va;
	size_t s;
	struct ihk_host_linux_device_data *data = file->private_data;

	pa = ihk_device_map_memory(data, *off, size);
	if ((long)pa <= 0) {
		return -EINVAL;
	}
	
	va = ihk_device_map_virtual(data, pa, size, NULL, 0);
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

	ihk_device_unmap_virtual(data, va, size);

	return s;
}

/** \brief write handler for the device file */
static long ihk_host_device_write(struct file *file, const char __user *buf,
                                  size_t size, loff_t *off)
{
	unsigned long pa;
	void *va;
	size_t s;
	struct ihk_host_linux_device_data *data = file->private_data;

	pa = ihk_device_map_memory(data, *off, size);
	if ((long)pa <= 0) {
		return -EINVAL;
	}
	
	va = ihk_device_map_virtual(data, pa, size, NULL, 0);
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

	ihk_device_unmap_virtual(data, va, size);

	return s;
}

struct ihk_host_map_data {
	int count;
	unsigned long pa;
};

static void ihk_host_device_mmap_open(struct vm_area_struct *vma)
{
	struct ihk_host_map_data *md = vma->vm_private_data;

	dprint_func_enter;
	md->count++;

	dprint_var_i4(md->count);
}

static void ihk_host_device_mmap_close(struct vm_area_struct *vma)
{
	struct ihk_host_map_data *md = vma->vm_private_data;
	struct ihk_host_linux_device_data *data = vma->vm_file->private_data;

	dprint_func_enter;
	dprint_var_i4(md->count);

	if ((--md->count) > 0) {
		return;
	}

	ihk_device_unmap_memory(data, md->pa, vma->vm_end - vma->vm_start);
	kfree(md);
}
	
static struct vm_operations_struct ihk_host_mmap_ops = {
	.open = ihk_host_device_mmap_open,
	.close = ihk_host_device_mmap_close,
};

/** \brief mmap handler for the device file */
int ihk_host_device_mmap(struct file *file, struct vm_area_struct *vma)
{
	unsigned long pa;
	struct ihk_host_linux_device_data *data = file->private_data;
	struct ihk_host_map_data *md;
	int r;

	dprint_func_enter;
	dprint_var_x8(vma->vm_pgoff);

	pa = ihk_device_map_memory(data, vma->vm_pgoff << PAGE_SHIFT,
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
	vma->vm_ops = &ihk_host_mmap_ops;
		
	ihk_host_device_mmap_open(vma);

	return r;
}

static struct file_operations mcd_cdev_ops = {
	.open = ihk_host_device_open,
	.read = ihk_host_device_read,
	.write = ihk_host_device_write,
	.mmap = ihk_host_device_mmap,
	.unlocked_ioctl = ihk_host_device_ioctl,
	.release = ihk_host_device_release,
};

static int ihk_panic(struct notifier_block *this, unsigned long ev, void *ptr)
{
	int i;

	for (i = 0; i < os_max_minor; i++) {
		if (!os_data[i])
			continue;
		if (os_data[i]->ops->panic_notifier)
			os_data[i]->ops->panic_notifier(os_data[i],
			                                os_data[i]->priv);
	}

	return NOTIFY_DONE;
}

static struct notifier_block ihk_panic_block = {
	.notifier_call = ihk_panic,
};

/** \brief Initialization function of the IHK-Host drivers.
 *
 * This function registers character device classes, and gets prepared to
 * create new device files. */
static int __init ihk_host_driver_init(void)
{
	if (alloc_chrdev_region(&mcd_dev_num, 0, DEV_MAX_MINOR, 
	                        DEV_DEV_NAME) < 0) {
		printk(KERN_INFO "IHK: Cannot allocate char device number.\n");
		goto ERR;
	}
	
	mcd_class = class_create(THIS_MODULE, DEV_DEV_NAME);
	if (!mcd_class) {
		printk(KERN_INFO "IHK: Cannot create mcd.\n");
		goto ERR;
	}
	
	if (alloc_chrdev_region(&mcos_dev_num, 0, OS_MAX_MINOR,
	                        OS_DEV_NAME) < 0) {
		printk(KERN_INFO "IHK: Cannot allocate char device number.\n");
		goto ERR;
	}
	mcos_class = class_create(THIS_MODULE, OS_DEV_NAME);
	if (!mcos_class) {
		printk(KERN_INFO "IHK: Cannot create mcos.\n");
		goto ERR;
	}

	INIT_LIST_HEAD(&ihk_os_notifiers);

	atomic_notifier_chain_register(&panic_notifier_list, &ihk_panic_block);

	INIT_LIST_HEAD(&ihk_kmsg_bufs);
	spin_lock_init(&ihk_kmsg_bufs_lock);

	printk("IHK Initialized: Device number: Device %x, OS %x\n",
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
core_initcall(ihk_host_driver_init);
#else
static int __init ihk_init(void)
{
	return ihk_host_driver_init();
}

static void __exit ihk_exit(void)
{
	/* XXX: TODO */
	int i;
	ihk_device_t dev;
	unsigned long flags;

	atomic_notifier_chain_unregister(&panic_notifier_list, &ihk_panic_block);

	for (i = 0; i < dev_max_minor; i++) {
		spin_lock_irqsave(&dev_data_lock, flags);
		dev = dev_data[i];
		dev_data[i] = DEV_DATA_INVALID;
		spin_unlock_irqrestore(&dev_data_lock, flags);

		if (dev && dev == DEV_DATA_INVALID) {
			ihk_unregister_device(dev);
		}
	}

	if (mcos_class)
		class_destroy(mcos_class);
	if (mcos_dev_num)
		unregister_chrdev_region(mcos_dev_num, OS_MAX_MINOR);
	if (mcd_class)
		class_destroy(mcd_class);
	if (mcd_dev_num)
		unregister_chrdev_region(mcd_dev_num, DEV_MAX_MINOR);

	return;
}

module_init(ihk_init);
module_exit(ihk_exit);

MODULE_LICENSE("GPL v2");
#endif

/*
 * IHK public function implementations
 */
ihk_device_t ihk_register_device(struct ihk_register_device_data *param)
{
	struct ihk_host_linux_device_data *data;
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

	printk("IHK: Device %s registered. /dev/%s%d created.\n",
	       data->name, DEV_DEV_NAME, minor);

	return data;
}

int ihk_unregister_device(ihk_device_t ihkdev)
{
	struct ihk_host_linux_device_data *data = ihkdev;
	unsigned long flags;

	if (atomic_read(&data->refcount) > 0) {
		return -EBUSY;
	}
	if (__destroy_all_os(data) != 0) {
		return -EBUSY;
	}

	/* Release stray kmsg_bufs */
	spin_lock_irqsave(&ihk_kmsg_bufs_lock, flags);
	while (!list_empty(&ihk_kmsg_bufs)) {
		struct ihk_kmsg_buf_container *cont;
		cont = list_first_entry(&ihk_kmsg_bufs, struct ihk_kmsg_buf_container, list);
		ekprintf("%s: Warning: stray kmsg_buf %p freed\n", __FUNCTION__, cont);
		delete_kmsg_buf(cont);
	}
	spin_unlock_irqrestore(&ihk_kmsg_bufs_lock, flags);

	cdev_del(&data->cdev);
	device_destroy(mcd_class, data->dev_num);

	if (data->ops->exit) {
		data->ops->exit(data, data->priv);
	}

	printk("IHK: Device %s unregistered.\n", data->name);
	dev_data[data->minor] = NULL;

	kfree(data->name);
	kfree(data);

	return 0;
}

ihk_os_t ihk_device_create_os(ihk_device_t data, unsigned long arg)
{
	int ret;

	ret = __ihk_device_create_os(data, arg);
	if (ret >= 0) {
		return os_data[ret];
	} else {
		return NULL;
	}
}

ihk_dma_channel_t ihk_device_get_dma_channel(ihk_device_t data, int channel)
{
	return __ihk_device_get_dma_channel(data, channel);
}

int ihk_device_get_dma_info(ihk_device_t data, struct ihk_dma_info *info)
{
	return __ihk_device_get_dma_info(data, info);
}

int ihk_os_boot(ihk_os_t os, int flag)
{
	return __ihk_os_boot(os, flag);
}

int ihk_os_shutdown(ihk_os_t os, int flag)
{
	return __ihk_os_shutdown(os, flag);
}

int ihk_os_load_memory(ihk_os_t os, char *buf, unsigned long size,
                       long offset) {
	return __ihk_os_load_memory(os, buf, size, offset);
}

int ihk_os_load_file(ihk_os_t os, char *fn) {
	return __ihk_os_load_file(os, fn);
}

int ihk_os_register_interrupt_handler(ihk_os_t os, int itype,
                                      struct ihk_host_interrupt_handler *h)
{
	return __ihk_os_register_handler(os, itype, h);
}

int ihk_os_unregister_interrupt_handler(ihk_os_t os, int itype,
                                        struct ihk_host_interrupt_handler *h)
{
	return __ihk_os_unregister_handler(os, itype, h);
}

int ihk_os_wait_for_status(ihk_os_t os, enum ihk_os_status status,
                           int sleepable, int timeout)
{
	return __ihk_os_wait_for_status(os, status, sleepable, timeout);
}

int ihk_os_get_special_address(ihk_os_t os, enum ihk_special_addr_type type,
                               unsigned long *pa, unsigned long *size)
{
	return __ihk_os_get_special_addr(os, type, pa, size);
}

unsigned long ihk_os_map_memory(ihk_os_t os, unsigned long pa,
                                unsigned long size)
{
	/* XXX: PAGE_SIZE should be device-specific */
	unsigned long st, ed, offset, r;

	offset = pa & (PAGE_SIZE - 1);
	st = pa & PAGE_MASK;
	ed = (pa + size + PAGE_SIZE - 1) & PAGE_MASK;
	
	r = __ihk_os_map_memory(os, st, ed - st);
	if ((long) r <= 0) {
		return r;
	}
		
	return r + offset;
}

int ihk_os_unmap_memory(ihk_os_t os, unsigned long pa, unsigned long size)
{
	/* XXX: PAGE_SIZE should be device-specific */
	unsigned long st, ed;

	st = pa & PAGE_MASK;
	ed = (pa + size + PAGE_SIZE - 1) & PAGE_MASK;
	
	return __ihk_os_unmap_memory(os, st, ed - st);
}

int ihk_os_issue_interrupt(ihk_os_t os, int cpu, int vector)
{
	return __ihk_os_issue_interrupt(os, cpu, vector);
}

int ihk_os_send_nmi(ihk_os_t os, int mode)
{
	return __ihk_os_send_nmi(os, mode);
}

unsigned long ihk_device_map_memory(ihk_device_t dev, unsigned long pa,
                                    unsigned long size)
{
	/* XXX: PAGE_SIZE should be device-specific */
	unsigned long st, ed, offset, r;

	offset = pa & (PAGE_SIZE - 1);
	st = pa & PAGE_MASK;
	ed = (pa + size + PAGE_SIZE - 1) & PAGE_MASK;
	
	r = __ihk_device_map_memory(dev, st, ed - st);
	if ((long) r <= 0) {
		return r;
	}
		
	return r + offset;
}

int ihk_device_unmap_memory(ihk_device_t dev, unsigned long pa,
                            unsigned long size)
{
	/* XXX: PAGE_SIZE should be device-specific */
	unsigned long st, ed;

	st = pa & PAGE_MASK;
	ed = (pa + size + PAGE_SIZE - 1) & PAGE_MASK;
	
	return __ihk_device_unmap_memory(dev, st, ed - st);
}

void *ihk_device_map_virtual(ihk_device_t dev, unsigned long pa,
                           unsigned long size, void *virtual, int flag)
{
	return __ihk_device_map_virtual(dev, pa, size, virtual, flag);
}

int ihk_device_unmap_virtual(ihk_device_t dev, void *virtual,
                             unsigned long size)
{
	return __ihk_device_unmap_virtual(dev, virtual, size);
}

struct ihk_mem_info *ihk_os_get_memory_info(ihk_os_t os)
{
	return __ihk_os_get_memory_info(os);
}

struct ihk_cpu_info *ihk_os_get_cpu_info(ihk_os_t os)
{
	return __ihk_os_get_cpu_info(os);
}

void *ihk_os_get_rusage(ihk_os_t ihk_os)
{
	struct ihk_host_linux_os_data *os = ihk_os;
	setup_rusage(os);
	return os->rusage;
}

ihk_device_t ihk_os_to_dev(ihk_os_t os)
{
	return ((struct ihk_host_linux_os_data *)os)->dev_data;
}

ihk_device_t ihk_host_find_dev(int index)
{
	if (!dev_data[index] || dev_data[index] == DEV_DATA_INVALID) {
		return NULL;
	} else{
		return dev_data[index];
	}
}

ihk_os_t ihk_host_find_os(int index, ihk_device_t dev)
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

void ihk_host_print_os_kmsg(ihk_os_t os)
{
	int nread;
	char *buf;
	char *lines, *line;
	struct ihk_host_linux_os_data *data = (struct ihk_host_linux_os_data *)os;

	buf = kmalloc(IHK_KMSG_SIZE, GFP_KERNEL);
	if (!buf) {
		goto out;
	}
	if (!os)
		goto out;

	nread = read_kmsg(data->kmsg_buf_container->kmsg_buf, buf, 0);

	if (nread < 0) {
		printk("%s: kmsg_buf is not available\n", __FUNCTION__);
		goto out;
	}

	/* Print line-by-line */
	lines = buf;
	line = strsep(&lines, "\n");
	while (line) {
		printk("%s\n", line);
		line = strsep(&lines, "\n");
	}

	if (nread == 0) {
		printk("%s: kmsg buffer is empty\n", __FUNCTION__);
	}
 out:
	if (buf) {
		kfree(buf);
	}
}

void ihk_host_os_set_usrdata(ihk_os_t ihk_os, void *data)
{
	struct ihk_host_linux_os_data *os = ihk_os;
	os -> usrdata = data;
}

void *ihk_host_os_get_usrdata(ihk_os_t ihk_os)
{
	struct ihk_host_linux_os_data *os = ihk_os;
	return os -> usrdata;
}

int ihk_host_os_get_index(ihk_os_t ihk_os)
{
	struct ihk_host_linux_os_data *os = ihk_os;
	int i;
	unsigned long flags;

	spin_lock_irqsave(&os_data_lock, flags);

	for (i = 0; i < OS_MAX_MINOR; ++i) {
		if (os_data[i] == os) {
			spin_unlock_irqrestore(&os_data_lock, flags);
			return i;
		}
	}

	spin_unlock_irqrestore(&os_data_lock, flags);
	return -1;
}

int ihk_os_set_kernel_call_handlers(ihk_os_t ihk_os,
	struct ihk_os_kernel_call_handler *handlers)
{
	struct ihk_host_linux_os_data *os = ihk_os;
	os->kernel_handlers = handlers;

	return 0;
}

int ihk_os_clear_kernel_call_handlers(ihk_os_t ihk_os)
{
	struct ihk_host_linux_os_data *os = ihk_os;
	os->kernel_handlers = NULL;

	return 0;
}

int ihk_os_read_cpu_register(ihk_os_t ihk_os, int cpu,
		struct ihk_os_cpu_register *desc)
{
	struct ihk_host_linux_os_data *os = ihk_os;

	if (!os || !os->kernel_handlers ||
			!os->kernel_handlers->read_cpu_register) {
		return -EINVAL;
	}

	return os->kernel_handlers->read_cpu_register(ihk_os, cpu, desc);
}

int ihk_os_write_cpu_register(ihk_os_t ihk_os, int cpu,
		struct ihk_os_cpu_register *desc)
{
	struct ihk_host_linux_os_data *os = ihk_os;

	if (!os || !os->kernel_handlers ||
			!os->kernel_handlers->write_cpu_register) {
		return -EINVAL;
	}

	return os->kernel_handlers->write_cpu_register(ihk_os, cpu, desc);
}

/*
 *  Returns LWK OS instance and CPU number of the system call offload
 *  origin.
 *  Arguments:
 *    ihk_os (OUTPUT):	LWK OS instance of system call offload origin
 *    cpu (OUTPUT):	CPU number of the system call offload origin
 *  Return value:
 *    0:		Caller is performing system call offload
 *    -EINVAL:		Caller isn't performing system call offload
 */
int ihk_get_request_os_cpu(ihk_os_t *ihk_os, int *cpu)
{
	struct ihk_host_linux_os_data *os;

	if (ihk_os == NULL) {
		return -EFAULT;
	}

	/*
	 * Look up IHK OS structure
	 * TODO: iterate all possible indeces, currently only for OS 0
	 */
	os = (struct ihk_host_linux_os_data *)ihk_host_find_os(0, NULL);
	if (!os) {
		dprintf("%s: not on system call offloading path\n",
			__func__);
		return -EINVAL;
	}

	if (!os->kernel_handlers ||
			!os->kernel_handlers->get_request_cpu) {
		return -EINVAL;
	}

	*ihk_os = (ihk_os_t *)os;
	return os->kernel_handlers->get_request_cpu(os, cpu);
}


int ihk_os_register_user_call_handlers(ihk_os_t ihk_os,
                                       struct ihk_os_user_call *clist)
{
	int i;
	unsigned long flags;
	struct ihk_host_linux_os_data *os = ihk_os;

	INIT_LIST_HEAD(&clist->list);
	for (i = 0; i < clist->num_handlers; i++) {
		if (clist->handlers[i].request < IHK_OS_AUX_CALL_START ||
		    clist->handlers[i].request > IHK_OS_AUX_CALL_END) {
			return -EINVAL;
		}
	}

	spin_lock_irqsave(&os->lock, flags);
	list_add_tail(&clist->list, &os->aux_call_list);
	spin_unlock_irqrestore(&os->lock, flags);

	return 0;
}

void ihk_os_unregister_user_call_handlers(ihk_os_t ihk_os,
                                          struct ihk_os_user_call *clist)
{
	struct ihk_host_linux_os_data *os = ihk_os;
	unsigned long flags;

	spin_lock_irqsave(&os->lock, flags);
	list_del(&clist->list);
	spin_unlock_irqrestore(&os->lock, flags);
}

int ihk_dma_request(ihk_dma_channel_t ihk_ch, struct ihk_dma_request *req)
{
	struct ihk_dma_channel *adc = ihk_ch;

	if (adc->ops->request) {
		return adc->ops->request(ihk_ch, req);
	} else {
		return -EINVAL;
	}
}

struct device *ihk_os_get_linux_device(ihk_os_t ihk_os)
{
	struct ihk_host_linux_os_data *os = ihk_os;

	return os->lindev;
} /* ihk_os_get_linux_device() */

struct ihk_cpu_topology *ihk_device_get_cpu_topology(ihk_device_t dev, int hw_id)
{
	return __ihk_device_get_cpu_topology(dev, hw_id);
}

struct ihk_node_topology *ihk_device_get_node_topology(ihk_device_t dev, int node)
{
	return __ihk_device_get_node_topology(dev, node);
}

int ihk_device_linux_cpu_to_hw_id(ihk_device_t dev, int cpu)
{
	return __ihk_device_linux_cpu_to_hw_id(dev, cpu);
}

int ihk_host_register_os_notifier(struct ihk_os_notifier *ion)
{
	int registered = 0;
	struct ihk_os_notifier *_ion;

	/* Check if registered already and add if not */
	if (down_interruptible(&ihk_os_notifiers_lock)) {
		return -ERESTARTSYS;
	}

	list_for_each_entry(_ion, &ihk_os_notifiers, nlist) {
		if (_ion == ion) {
			registered = 1;
			break;
		}
	}

	if (!registered) {
		list_add_tail(&ion->nlist, &ihk_os_notifiers);
		printk("IHK: OS notifier added\n");
	}

	up(&ihk_os_notifiers_lock);
	return 0;
}

int ihk_host_deregister_os_notifier(struct ihk_os_notifier *ion)
{
	int registered = 0;
	struct ihk_os_notifier *_ion;

	/* Check if registered already and remove if yes */
	if (down_interruptible(&ihk_os_notifiers_lock)) {
		return -ERESTARTSYS;
	}

	list_for_each_entry(_ion, &ihk_os_notifiers, nlist) {
		if (_ion == ion) {
			registered = 1;
			break;
		}
	}

	if (registered) {
		list_del(&ion->nlist);
		printk("IHK: OS notifier removed\n");
	}

	up(&ihk_os_notifiers_lock);
	return 0;
}

EXPORT_SYMBOL(ihk_register_device);
EXPORT_SYMBOL(ihk_unregister_device);
EXPORT_SYMBOL(ihk_device_create_os);
EXPORT_SYMBOL(ihk_os_load_file);
EXPORT_SYMBOL(ihk_os_load_memory);
EXPORT_SYMBOL(ihk_os_boot);
EXPORT_SYMBOL(ihk_os_shutdown);
EXPORT_SYMBOL(ihk_os_register_interrupt_handler);
EXPORT_SYMBOL(ihk_os_unregister_interrupt_handler);
EXPORT_SYMBOL(ihk_os_get_special_address);
EXPORT_SYMBOL(ihk_os_wait_for_status);
EXPORT_SYMBOL(ihk_host_find_dev);
EXPORT_SYMBOL(ihk_host_find_os);
EXPORT_SYMBOL(ihk_host_print_os_kmsg);
EXPORT_SYMBOL(ihk_host_os_set_usrdata);
EXPORT_SYMBOL(ihk_host_os_get_usrdata);
EXPORT_SYMBOL(ihk_host_os_get_index);
EXPORT_SYMBOL(ihk_os_to_dev);
EXPORT_SYMBOL(ihk_device_map_virtual);
EXPORT_SYMBOL(ihk_device_unmap_virtual);
EXPORT_SYMBOL(ihk_device_map_memory);
EXPORT_SYMBOL(ihk_device_unmap_memory);
EXPORT_SYMBOL(ihk_os_issue_interrupt);
EXPORT_SYMBOL(ihk_os_send_nmi);
EXPORT_SYMBOL(ihk_os_register_user_call_handlers);
EXPORT_SYMBOL(ihk_os_unregister_user_call_handlers);
EXPORT_SYMBOL(ihk_os_set_kernel_call_handlers);
EXPORT_SYMBOL(ihk_get_request_os_cpu);
EXPORT_SYMBOL(ihk_os_read_cpu_register);
EXPORT_SYMBOL(ihk_os_write_cpu_register);
EXPORT_SYMBOL(ihk_os_clear_kernel_call_handlers);
EXPORT_SYMBOL(ihk_os_get_memory_info);
EXPORT_SYMBOL(ihk_os_get_cpu_info);
EXPORT_SYMBOL(ihk_device_get_dma_channel);
EXPORT_SYMBOL(ihk_device_get_dma_info);
EXPORT_SYMBOL(ihk_dma_request);
EXPORT_SYMBOL(ihk_os_register_release_handler);
EXPORT_SYMBOL(ihk_os_set_mcos_private_data);
EXPORT_SYMBOL(ihk_os_get_mcos_private_data);
EXPORT_SYMBOL(ihk_os_get_linux_device);
EXPORT_SYMBOL(ihk_device_get_cpu_topology);
EXPORT_SYMBOL(ihk_device_get_node_topology);
EXPORT_SYMBOL(ihk_device_linux_cpu_to_hw_id);
EXPORT_SYMBOL(ihk_host_register_os_notifier);
EXPORT_SYMBOL(ihk_host_deregister_os_notifier);
EXPORT_SYMBOL(ihk_os_eventfd);
EXPORT_SYMBOL(ihk_os_get_rusage);
