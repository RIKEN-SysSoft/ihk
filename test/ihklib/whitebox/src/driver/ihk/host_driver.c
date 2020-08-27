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
#include <linux/timer.h>
#include <linux/jiffies.h>
#include "driver/ihk_host_user.h"
#include "driver/ihk_host_driver.h"
#include <asm/spinlock.h>
#include <ihk/misc/debug.h>
#include "host_linux.h"
#include "ops_wrappers.h"
#include <config.h>

#include "driver/okng_driver.h"
#include "driver/fs_utils.h"
#include "branch_info.h"
#include "arr_utils.h"

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

static int __ihk_os_load_memory_orig(struct ihk_host_linux_os_data *data,
                                     char *buf, unsigned long size, long offset)
{
  if (data->ops->load_mem) {
    return data->ops->load_mem(data, data->priv, buf, size, offset);
  } else{
    return -EINVAL;
  }
}

/** \brief load_memory operation for an OS device file */
static int __ihk_os_load_memory(struct ihk_host_linux_os_data *data,
                                char *buf, unsigned long size, long offset)
{
  if (g_ihk_test_mode != TEST__IHK_OS_LOAD_MEMORY)  // Disable test code
    return __ihk_os_load_memory_orig(data, buf, size, offset);

  unsigned long ivec = 0;
  unsigned long total_branch = 2;

  branch_info_t b_infos[] = {
    { -EINVAL, "invalid handler" },
    { 0,       "main case" },
  };

  int ret;

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    if (ivec != 0 && (ivec == 1 && data->ops->load_mem)) {
      ret = data->ops->load_mem(data, data->priv, buf, size, offset);
    } else {  // ivec = 0
      ret = -EINVAL;
      if (ivec != 0) return ret;
      goto out;
    }

   out:
    BRANCH_RET_CHK(ret, b_infos[ivec].expected);
  }
  return ret;
 err:
  return -EINVAL;
}

static int __ihk_os_load_file_orig(struct ihk_host_linux_os_data *data, char *fn)
{
  char *buf;
  struct file *file;
  int ret = 0;
  loff_t size, done, pos = 0;
  long r;

  int use_load_mem = (g_ihk_test_mode == TEST_SMP_IHK_OS_LOAD_MEM ||
                      g_ihk_test_mode == TEST__IHK_OS_LOAD_MEMORY) ? 1 : 0;

  if (!use_load_mem && data->ops->load_file) {
    dprintf("IHK: os_load_file is defined. Use it.\n");

    ret = data->ops->load_file(data, data->priv, fn);
  } else if (data->ops->load_mem) {
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

/** \brief load_file operation for an OS device file
 *
 * This function is called when a user requests to load the kernel image
 * directly from a file.
 * If the IHK OS driver does not provide a handler for load_file,
 * it uses the load_mem handler instead.
 */
static int __ihk_os_load_file(struct ihk_host_linux_os_data *data, char *fn)
{
  if (g_ihk_test_mode != TEST__IHK_OS_LOAD_FILE)  // Disable test code
    return __ihk_os_load_file_orig(data, fn);

  unsigned long ivec = 0;
  unsigned long total_branch = 7;

  branch_info_t b_infos[] = {
    { -EINVAL, "No loading function is defined" },
    { -ENOENT, "filp_open fail"  },
    { -EINVAL, "i_size_read fail" },
    { -EFAULT, "kernel_read fail" },
    { -EINVAL, "__ihk_os_load_memory fail" },
    { 0,       "load_mem success" },
    { 0,       "load_file success" },
  };

  void *load_file_handler = data->ops->load_file;
  void *load_mem_handler = data->ops->load_mem;
  int ret = 0;

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    char *buf;
    struct file *file;
    ret = 0;
    loff_t size = 0, done = 0, pos = 0;
    long r;
    int should_quit = 0;

    if (ivec <= 5) data->ops->load_file = NULL;
    if (ivec == 0) data->ops->load_mem = NULL;

    if (data->ops->load_file) {  // main case
      dprintf("IHK: os_load_file is defined. Use it.\n");

      ret = data->ops->load_file(data, data->priv, fn);
    } else if (data->ops->load_mem) {  // ivec >= 1
      if (ivec > 4)
        dprintf("IHK: os_load_mem is defined. Use it.\n");

      file = filp_open(fn, O_RDONLY, 0);
      if (ivec == 1 || IS_ERR(file)) {
        ret = -ENOENT;
        if (ivec != 1) {
          dprintf("IHK: file not found %s\n", fn);
          return ret;
        }
        goto out;
      }

      size = i_size_read(file->f_path.dentry->d_inode);
      if (ivec == 2 || size <= 0) {
        ret = -EINVAL;
        if (ivec != 2) {
          fput(file);
          dprintf("IHK: file size invalid: %lld\n", size);
          return ret;
        }
        goto out;
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
        if (ivec == 3 || r <= 0) {
          ret = -EFAULT;
          if (ivec != 3) {
            dprintf("kernel_read failed: %ld\n", r);
            ret = (int)r;
            should_quit = 1;
          }
          break;
        }

        ret = __ihk_os_load_memory(data, buf, r, done);
        if (ivec == 4 || ret) {
          ret = -EINVAL;
          if (ivec != 4) should_quit = 1;
          break;
        }

        done += r;
      }

      fput(file);
    } else {  // ivec = 0
      ret = -EINVAL;
      if (ivec != 0) {
        dprintf("IHK: No loading function is defined.\n");
        return ret;
      }
      goto out;
    }

   out:
    if (should_quit) return ret;

    BRANCH_RET_CHK(ret, b_infos[ivec].expected);

    if (ivec >= 1 && ivec <= 4) {
      OKNG(done == 0, "not load any mem\n");
    } else if (ivec == 5) {
      OKNG(size > 0, "file size to load is valid\n");
      OKNG(done == size, "load mem success\n");
    }

    /* reset state */
    data->ops->load_file = load_file_handler;
    data->ops->load_mem = load_mem_handler;
  }

  return ret;
 err:
  return -EINVAL;
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

static int  __ihk_os_boot_orig(struct ihk_host_linux_os_data *data, int flag)
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

/** \brief Boot a kernel related to the OS file */
static int  __ihk_os_boot(struct ihk_host_linux_os_data *data, int flag)
{
  if (g_ihk_test_mode != TEST__IHK_OS_BOOT)  // Disable test code
    return __ihk_os_boot_orig(data, flag);

  unsigned long ivec = 0;
  unsigned long total_branch = 9;

  branch_info_t b_infos[] = {
    { -EINVAL,      "ihk_kmsg_bufs is empty" },
    { -EINVAL,      "not found a valid kmsg bufs container" },
    { -ERESTARTSYS, "cannot acquire OS notifier lock" },
    { -EINVAL,      "invalid OS boot handler" },
    { -EINVAL,      "ihk_os_notifiers is empty" },
    { -EINVAL,      "ihk_ikc_master_init fail" },
    { -EINVAL,      "invalid OS notifier handler" },
    { -EINVAL,      "OS notifier boot fail" },
    { 0,            "main case" },
  };

  unsigned long flags = 0;
  int ret;

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    int index = ihk_host_os_get_index(data);
    int found = 0;
    int should_quit = 0;
    struct ihk_kmsg_buf_container *cont = NULL;
    ret = 0;

    /* Get the latest kmsg_buf */
    spin_lock_irqsave(&ihk_kmsg_bufs_lock, flags);
    if (ivec == 0 || list_empty(&ihk_kmsg_bufs)) {
      // through
    } else list_for_each_entry_reverse(cont, &ihk_kmsg_bufs, list) {
      if (ivec != 1 && cont->os_index == data->minor) {
        data->kmsg_buf_container = cont;
        dkprintf("%s: got kmsg_buf %p\n", __FUNCTION__, cont);
        atomic_inc(&cont->count); /* OS instance is referring to it */
        found = 1;
        break;
      }
    }
    spin_unlock_irqrestore(&ihk_kmsg_bufs_lock, flags);

    if (!found) {  // ivec = 0|1 go here
      ret = -EINVAL;
      if (ivec > 1) return ret;
      goto out;
    }

    /*
     * Take OS notifiers lock here so that we can safely
     * return on a signal..
     */
    if (ivec == 2 || down_interruptible(&ihk_os_notifiers_lock)) {
      ret = -ERESTARTSYS;
      if (ivec != 2) should_quit = 1;
      goto out;
    }

    if (ivec == 3 || !data->ops->boot) {
      ret = -EINVAL;
      if (ivec != 3) should_quit = 1;
      // through
    } else if (data->ops->boot) {
      if (ivec == 4 || list_empty(&ihk_os_notifiers)) {
        ret = -EINVAL;
        if (ivec != 4) should_quit = 1;
        goto out_up;
      }
      if (ivec > 7)
        ret = data->ops->boot(data, data->priv, flag);
      if (ivec >= 5 || ret == 0) {
        if (ivec > 7)
          ret = ihk_ikc_master_init(data);
        if (ivec == 5 || ret) {
          ret = -EINVAL;
          if (ivec != 5) should_quit = 1;
          goto out_up;
        }
      }

      /* Call OS notifiers */
      if (ivec >= 6 || ret == 0) {
        struct ihk_os_notifier *_ion;
        list_for_each_entry(_ion, &ihk_os_notifiers, nlist) {
          if (ivec == 6 || !_ion->ops || !_ion->ops->boot) {
            ret = -EINVAL;
            if (ivec != 6) should_quit = 1;
            goto out_up;
          }
          if (ivec > 7)
            ret = _ion->ops->boot(index);
          if (ivec == 7 || ret) {
            ret = -EINVAL;
            if (ivec != 7) {
              should_quit = 1;
              ikc_master_finalize(data);
              data->ops->shutdown(data, data->priv, flag);
            }
            goto out_up;
          }
        }
      }
      if (ret) should_quit = 1;
    }

   out_up:
    up(&ihk_os_notifiers_lock);
   out:
    if ((ivec > 1 && ivec < total_branch - 1) || should_quit)
      atomic_dec(&cont->count); // reset refcnt of kmsg bufs container
    if (should_quit) return ret;

    BRANCH_RET_CHK(ret, b_infos[ivec].expected);

    if (ivec == total_branch - 1) {
      OKNG(atomic_read(&cont->count) == 1, "check refcnt of kmsg buf container\n");
      /* fs */
      OKNG(fs_os_procfs_entry_exist(index), "os procfs entries exist\n");
      OKNG(fs_os_sysfs_entry_exist(index), "os sysfs entries exist\n");
      /* notifier */
      OKNG(data->kernel_handlers != NULL,
           "kernel call handlers should not be cleared\n");
      OKNG(!list_empty(&data->aux_call_list),
           "List of the additional ioctl handlers should not be empty\n");
    } else {
      if (cont) {
        OKNG(atomic_read(&cont->count) == 0, "check refcnt of kmsg buf container\n");
      }
      /* fs */
      OKNG(!fs_os_procfs_entry_exist(index), "os procfs entries are not created\n");
      OKNG(!fs_os_sysfs_entry_exist(index), "os sysfs entries are not created\n");
      /* notifier */
      OKNG(data->kernel_handlers == NULL,
           "kernel call handlers should be cleared\n");
      OKNG(list_empty(&data->aux_call_list),
           "List of the additional ioctl handlers should be empty\n");

    }
  }
  return ret;
 err:
  return -EINVAL;
}

static void delete_kmsg_buf_orig(struct ihk_kmsg_buf_container* cont) {
  if (!cont) {
    return;
  }

  __free_pages(virt_to_page(cont->kmsg_buf), cont->order);
  dkprintf("%s: __free_pages kmsg_buf\n", __FUNCTION__);

  list_del(&cont->list);
  kfree(cont);
  dkprintf("%s: kmsg_buf %p deleted\n", __FUNCTION__, cont);
}

static void delete_kmsg_buf(struct ihk_kmsg_buf_container* cont) {
  if (g_ihk_test_mode != TEST_DELETE_KMSG_BUF)  // Disable test code
    return delete_kmsg_buf_orig(cont);

  unsigned long ivec = 0;
  unsigned long total_branch = 2;

  branch_info_t b_infos[] = {
    { 0, "invalid container" },
    { 0, "main case" },
  };

  struct ihk_kmsg_buf_container* _cont;
  int count_nbuf_before = 0, count_nbuf_after = 0;
  list_for_each_entry(_cont, &ihk_kmsg_bufs, list) {
    count_nbuf_before++;
  }

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    count_nbuf_after = 0;
    if (ivec == 0 || !cont) {
      if (ivec != 0) return;
      goto out;
    }

    __free_pages(virt_to_page(cont->kmsg_buf), cont->order);
    dkprintf("%s: __free_pages kmsg_buf\n", __FUNCTION__);

    list_del(&cont->list);
    kfree(cont);
    dkprintf("%s: kmsg_buf %p deleted\n", __FUNCTION__, cont);

   out:
    list_for_each_entry(_cont, &ihk_kmsg_bufs, list) {
      count_nbuf_after++;
    }

    if (ivec == total_branch - 1) {
      OKNG(count_nbuf_after == count_nbuf_before - 1,
           "the number of kmsg buffers should be decreased by 1\n");
    } else {
      OKNG(count_nbuf_after == count_nbuf_before,
           "the number of kmsg buffers should be unchanged\n");
    }
  }

 err:
  return;
}

static int release_kmsg_buf_orig(struct ihk_kmsg_buf_container* cont)
{
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

static int release_kmsg_buf(struct ihk_kmsg_buf_container* cont)
{
  if (g_ihk_test_mode != TEST_RELEASE_KMSG_BUF)  // Disable test code
    return release_kmsg_buf_orig(cont);

  unsigned long ivec = 0;
  unsigned long total_branch = 4;

  branch_info_t b_infos[] = {
    { -EINVAL, "invalid container" },
    { -EINVAL, "container refcnt is zero" },
    { -EBUSY,  "container is busy" },
    { 0,       "main case" },
  };

  int ret;
  unsigned long flags;
  struct ihk_kmsg_buf_container* _cont;
  int count_nbuf_before = 0;
  spin_lock_irqsave(&ihk_kmsg_bufs_lock, flags);
  list_for_each_entry(_cont, &ihk_kmsg_bufs, list) {
    count_nbuf_before++;
  }
  spin_unlock_irqrestore(&ihk_kmsg_bufs_lock, flags);

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    ret = 0;
    int refcnt = 0;
    int count_nbuf_after = 0;

    if (ivec == 0 || !cont) {
      ret = -EINVAL;
      if (ivec != 0) return ret;
      goto out;
    }

    if (ivec == 1 || atomic_read(&cont->count) == 0) {
      ret = -EINVAL;
      if (ivec != 1) {
        dkprintf("%s: Trying to unref kmsg_buf with count of zero\n", __FUNCTION__);
        return ret;
      }
      goto out;
    }

    spin_lock_irqsave(&ihk_kmsg_bufs_lock, flags);
    refcnt = atomic_read(&cont->count);
    if (ivec == 2 || refcnt > 1) {
      ret = -EBUSY;
      spin_unlock_irqrestore(&ihk_kmsg_bufs_lock, flags);
      goto out;
    } else if (atomic_dec_return(&cont->count) == 0) {
      delete_kmsg_buf(cont);
    }
    spin_unlock_irqrestore(&ihk_kmsg_bufs_lock, flags);

   out:
    BRANCH_RET_CHK(ret, b_infos[ivec].expected);

    spin_lock_irqsave(&ihk_kmsg_bufs_lock, flags);
    list_for_each_entry(_cont, &ihk_kmsg_bufs, list) {
      count_nbuf_after++;
    }
    spin_unlock_irqrestore(&ihk_kmsg_bufs_lock, flags);

    if (ivec == total_branch - 1) {
      OKNG(count_nbuf_after == count_nbuf_before - 1,
           "the number of kmsg buffers should be decreased by 1\n");
    } else {
      OKNG(count_nbuf_after == count_nbuf_before,
           "the number of kmsg buffers should be unchanged\n");
    }
  }

  return 0;
 err:
  return (ret)? ret : -EINVAL;
}

static int __ihk_os_status(struct ihk_host_linux_os_data *data);
static int __ihk_os_thaw(struct ihk_host_linux_os_data *data);

static int __ihk_os_shutdown_orig(struct ihk_host_linux_os_data *data, int flag)
{
  int ret = -EINVAL;
  struct ihk_os_notifier *_ion;
  int index = ihk_host_os_get_index(data);
  enum ihk_os_status status = __ihk_os_status(data);

  switch (status) {
  case IHK_OS_STATUS_SHUTDOWN:
    pr_err("%s: error: invalid os status: %d\n", __func__, status);
    ret = -EBUSY;
    goto out;
  case IHK_OS_STATUS_FREEZING:
    /* wait 10 sec for frozen */
    pr_info("%s: waiting for frozen...\n", __func__);
    if (ihk_os_wait_for_status((ihk_os_t)data, IHK_OS_STATUS_FROZEN,
             0, 100) != 0) {
      pr_info("%s: warning: wait for frozen timeouted\n", __func__);
    }
    /* fall through */
  case IHK_OS_STATUS_FROZEN:
    pr_info("%s: trying to thaw...\n", __func__);
    ret = __ihk_os_thaw(data);
    if (ret) {
      pr_err("%s: error: __ihk_os_thaw: %d\n", __func__, ret);
    }
    /* fall through */
  case IHK_OS_STATUS_BOOTING:
  case IHK_OS_STATUS_BOOTED:
  case IHK_OS_STATUS_READY:
    /* wait 20 sec for running */
    pr_info("%s: waiting for running...\n", __func__);
    if (ihk_os_wait_for_status((ihk_os_t)data,
             IHK_OS_STATUS_RUNNING,
             0, 200) != 0) {
      pr_info("%s: warning: wait for running timeouted, "
              "trying to shutdown with nmi...\n", __func__);

      /* send nmi to force shutdown */
      ihk_os_send_nmi((ihk_os_t)data, 3);
      mdelay(200);
    }
    break;
  case IHK_OS_STATUS_NOT_BOOTED:
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
      pr_err("%s: error: shutdown returned %d\n", __func__, ret);
      goto out;
    }
  }

  /* Release kmsg_buf */
  if (data->kmsg_buf_container) {
    struct ihk_kmsg_buf_container *cont = data->kmsg_buf_container;
    data->kmsg_buf_container = NULL;
    ret = release_kmsg_buf(cont);
    if (ret) {
      dprintf("%s: error: release_kmsg_buf returned %d\n", __func__, ret);
      goto out;
    }
  }

  printk("IHK: OS shutdown OK\n");
  ret = 0;
 out:
  return ret;
}

static int _get_cpus_status(struct ihk_host_linux_os_data *os,
                            int *cpus, int *status, int n)
{
  int ret, i;
  for (i = 0; i < n; i++) {
    ret = os->dev_data->ops->get_cpu_status(os->dev_data, cpus[i], status + i);
    if (ret) return ret;
  }
  return 0;
}

static int _check_cpus_status(int *status, int n, int target_status)
{
  int i;
  for (i = 0; i < n; i++) {
    if (status[i] != target_status) return 0;
  }
  return 1;
}

static struct ihk_kmsg_buf_container *_find_cont(int minor);

/** \brief Shutdown the kernel related to the OS file */
static int __ihk_os_shutdown(struct ihk_host_linux_os_data *data, int flag)
{
  if (g_ihk_test_mode != TEST__IHK_OS_SHUTDOWN)  // Disable test code
    return __ihk_os_shutdown_orig(data, flag);

  /* for keeping track of execution path */
  enum exec_path {
    PATH_OS_STATUS_SHUTDOWN         = 1UL << 0,
    PATH_OS_STATUS_FREEZING         = 1UL << 1,
    PATH_OS_STATUS_FROZEN           = 1UL << 2,
    PATH_OS_STATUS_READY            = 1UL << 3,
    PATH_OS_STATUS_RUNNING          = 1UL << 4,
    PATH_NOTIFIER_LOCK_ACQUIRED     = 1UL << 5,
    PATH_NOTIFY_SHUTDOWN            = 1UL << 6,
    PATH_OS_OPS_SHUTDOWN_FAILED     = 1UL << 7,
    PATH_OS_OPS_SHUTDOWN            = 1UL << 8,
    PATH_OS_RELEASE_KMSG_BUF        = 1UL << 9,
    PATH_OS_SHUTDOWN_FINISHED       = 1UL << 10,
  };
  unsigned long exec_path = 0UL;

  /* save previous state */
  // os status
  enum ihk_os_status os_status_prev = __ihk_os_status(data);
  enum ihk_os_status os_status_after;
  // cpus
  int ncpus_prev = __ihk_os_get_num_cpus(data);
  int ncpus_after;
  struct ihk_cpu_info *cpu_info = __ihk_os_get_cpu_info(data);
  int *cpus_status_prev = kmalloc(sizeof(int) * ncpus_prev, GFP_KERNEL);
  if (!cpus_status_prev) return -ENOMEM;
  int *cpus_status_after = kmalloc(sizeof(int) * ncpus_prev, GFP_KERNEL);
  if (!cpus_status_after) goto err;
  int ret = _get_cpus_status(data, cpu_info->mapping, cpus_status_prev, ncpus_prev);
  if (ret) goto err;
  // mem
  int n_chunks_prev = data->ops->get_num_mem_chunks(data, data->priv);
  int n_chunks_after;

  int *numa_mapping;
  pgd_t *boot_pt;

  START("main case");

  ret = -EINVAL;
  struct ihk_kmsg_buf_container *cont = NULL;
  struct ihk_os_notifier *_ion;
  int index = ihk_host_os_get_index(data);
  enum ihk_os_status status = __ihk_os_status(data);

  switch (status) {
  case IHK_OS_STATUS_SHUTDOWN:
    pr_err("%s: error: invalid os status: %d\n", __func__, status);
    exec_path |= PATH_OS_STATUS_SHUTDOWN;
    ret = -EBUSY;
    goto out;
  case IHK_OS_STATUS_FREEZING:
    /* wait 10 sec for frozen */
    pr_info("%s: waiting for frozen...\n", __func__);
    exec_path |= PATH_OS_STATUS_FREEZING;
    if (ihk_os_wait_for_status(
          (ihk_os_t)data, IHK_OS_STATUS_FROZEN, 0, 100) != 0) {
      pr_info("%s: warning: wait for frozen timeouted\n", __func__);
    }
    /* fall through */
  case IHK_OS_STATUS_FROZEN:
    pr_info("%s: trying to thaw...\n", __func__);
    exec_path |= PATH_OS_STATUS_FROZEN;
    ret = __ihk_os_thaw(data);
    if (ret) {
      pr_err("%s: error: __ihk_os_thaw: %d\n", __func__, ret);
    }
    /* fall through */
  case IHK_OS_STATUS_BOOTING:
  case IHK_OS_STATUS_BOOTED:
  case IHK_OS_STATUS_READY:
    /* wait 20 sec for running */
    pr_info("%s: waiting for running...\n", __func__);
    if (ihk_os_wait_for_status(
          (ihk_os_t)data, IHK_OS_STATUS_RUNNING, 0, 200) != 0) {
      pr_info("%s: warning: wait for running timeouted, "
              "trying to shutdown with nmi...\n", __func__);

      /* send nmi to force shutdown */
      ihk_os_send_nmi((ihk_os_t)data, 3);
      mdelay(200);
    }
    exec_path |= PATH_OS_STATUS_READY;
    break;
  case IHK_OS_STATUS_NOT_BOOTED:
  case IHK_OS_STATUS_RUNNING:
  case IHK_OS_STATUS_FAILED:
  case IHK_OS_STATUS_HUNGUP:
  default:
    exec_path |= PATH_OS_STATUS_RUNNING;
    break;
  }

  /* Call OS notifiers */
  if (down_interruptible(&ihk_os_notifiers_lock)) {
    ret = -ERESTARTSYS;
    goto out;
  }

  exec_path |= PATH_NOTIFIER_LOCK_ACQUIRED;

  if (index != -1) {
    list_for_each_entry(_ion, &ihk_os_notifiers, nlist) {
      if (_ion->ops && _ion->ops->shutdown) {
        _ion->ops->shutdown(index);
        exec_path |= PATH_NOTIFY_SHUTDOWN;
      }
    }
  }
  up(&ihk_os_notifiers_lock);

  ikc_master_finalize(data);

  if (data->ops->shutdown) {
    ret = data->ops->shutdown(data, data->priv, flag);
    if (ret) {
      pr_err("%s: error: shutdown returned %d\n", __func__, ret);
      exec_path |= PATH_OS_OPS_SHUTDOWN_FAILED;
      goto out;
    }
    exec_path |= PATH_OS_OPS_SHUTDOWN;
  }

  /* Release kmsg_buf */
  if (data->kmsg_buf_container) {
    cont = data->kmsg_buf_container;
    data->kmsg_buf_container = NULL;
    release_kmsg_buf(cont);
    exec_path |= PATH_OS_RELEASE_KMSG_BUF;
  }

  printk("IHK: OS shutdown OK\n");
  exec_path |= PATH_OS_SHUTDOWN_FINISHED;
  ret = 0;

 out:

  /* check state */
  cont = _find_cont(index);
  os_status_after = __ihk_os_status(data);
  ncpus_after = __ihk_os_get_num_cpus(data);
  ret = _get_cpus_status(data, cpu_info->mapping, cpus_status_after, ncpus_prev);
  if (ret) goto err;
  n_chunks_after = data->ops->get_num_mem_chunks(data, data->priv);
  numa_mapping = data->ops->get_numa_mapping(data, data->priv);
  boot_pt = data->ops->get_boot_pt(data, data->priv);

  if (exec_path & PATH_NOTIFY_SHUTDOWN) {
    OKNG(!fs_os_procfs_entry_exist(index), "os procfs entries are removed\n");
    OKNG(!fs_os_sysfs_entry_exist(index), "os sysfs entries are removed\n");
    OKNG(data->kernel_handlers == NULL,
         "kernel call handlers should be cleared\n");
    OKNG(list_empty(&data->aux_call_list),
         "List of the additional ioctl handlers should be empty\n");
    // topology
    /*struct mcctrl_usrdata *udp = ihk_host_os_get_usrdata(data);
    OKNG(list_empty(&udp->cpu_topology_list),
         "cpu topology list should be empty\n");
    OKNG(list_empty(&udp->node_topology_list),
         "node topology list should be empty\n");*/
  }

  if (exec_path & PATH_OS_OPS_SHUTDOWN) {
    // os status
    OKNG(os_status_after == IHK_OS_STATUS_NOT_BOOTED,
         "os status should be reset to not-booted\n");
    // cpus
    OKNG(ncpus_after == 0, "all cpus are reset\n");
    int suc = _check_cpus_status(cpus_status_after, ncpus_prev, IHK_CPU_STATUS_AVAILABLE);
    OKNG(suc, "all cpus status are available\n");
    // mem
    OKNG(n_chunks_after == 0, "all mem chunks are released\n");
    // numa-mapping
    OKNG(numa_mapping == NULL, "numa-mapping is released\n");
    // bootstrap table
    OKNG(boot_pt == NULL, "bootstrap table is released\n");
  }

  if (exec_path & PATH_OS_RELEASE_KMSG_BUF) {
    OKNG(!cont, "kmsg buf container should be released\n");
  }

  if (exec_path & PATH_OS_SHUTDOWN_FINISHED) {
    OKNG(data->ikc_initialized == 0, "ikc master channel should be finalized\n");
  }

  if (exec_path & PATH_OS_OPS_SHUTDOWN_FAILED
      || exec_path & PATH_OS_STATUS_SHUTDOWN
      || !(exec_path & PATH_NOTIFIER_LOCK_ACQUIRED)) {
    OKNG(!cont, "kmsg buf container should not be released\n");

    if (exec_path & PATH_OS_STATUS_SHUTDOWN
        || !(exec_path & PATH_NOTIFIER_LOCK_ACQUIRED)) {
      OKNG(ncpus_after == ncpus_prev, "the number of cpus should be unchanged\n");
      int suc = arr_equals(cpus_status_prev, cpus_status_after, ncpus_prev);
      OKNG(suc, "all cpus status must be unchanged\n");
      OKNG(n_chunks_after == n_chunks_prev,
           "the number of mem chunks should be unchanged\n");
      OKNG(numa_mapping != NULL, "numa-mapping is not released\n");
#ifdef X86_64
      OKNG(boot_pt != NULL, "bootstrap table is not released\n");
#endif

      /* notifier */
      OKNG(fs_os_procfs_entry_exist(index), "os procfs entries exist\n");
      OKNG(fs_os_sysfs_entry_exist(index), "os sysfs entries exist\n");
      OKNG(data->kernel_handlers != NULL,
           "kernel call handlers should not be cleared\n");
      OKNG(!list_empty(&data->aux_call_list),
           "List of the additional ioctl handlers should not be empty\n");
      /*struct mcctrl_usrdata *udp = ihk_host_os_get_usrdata(data);
      OKNG(!list_empty(&udp->cpu_topology_list),
           "cpu topology list should not be empty\n");
      OKNG(!list_empty(&udp->node_topology_list),
           "node topology list should not be empty\n");*/
    }
  }

 err:
  if (cpus_status_prev) kfree(cpus_status_prev);
  if (cpus_status_after) kfree(cpus_status_after);
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

static int read_kmsg_orig(struct ihk_kmsg_buf *kmsg_buf, char *buf, int shift)
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

static int read_kmsg(struct ihk_kmsg_buf *kmsg_buf, char *buf, int shift)
{
  if (g_ihk_test_mode != TEST_READ_KMSG)  // Disable test code
    return read_kmsg_orig(kmsg_buf, buf, shift);

  unsigned long ivec = 0;
  unsigned long total_branch = 2;

  branch_info_t b_infos[] = {
    { -EINVAL, "invalid parameter" },
    { 0,       "main case" },
  };

  enum exec_path {
    PATH_HEAD_OVER_TAIL    = 1UL << 0,
    PATH_TAIL_OVER_HEAD    = 1UL << 1,
  };
  unsigned long exec_path = 0UL;
  int len_bottom = 0, len_top = 0;

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    len_bottom = len_top = 0;
    int ret = 0;
    unsigned long flags;

    if (ivec == 0 || (!kmsg_buf || !buf)) {
      ret = -EINVAL;
      if (ivec != 0) return ret;
      goto out;
    }

    /* Inter-kernel lock for struct ihk_kmsg_buf */
    local_irq_save(flags);
    while(__sync_val_compare_and_swap(&kmsg_buf->lock, 0, 1) != 0) {
      cpu_relax();
    }

    if (kmsg_buf->head > kmsg_buf->tail) {
      len_bottom = strnlen(&kmsg_buf->str[kmsg_buf->head], kmsg_buf->len - kmsg_buf->head);
      len_top = kmsg_buf->tail;
      exec_path != PATH_HEAD_OVER_TAIL;
    } else {
      len_bottom = kmsg_buf->tail - kmsg_buf->head;
      len_top = 0;
      exec_path != PATH_TAIL_OVER_HEAD;
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

   out:
    BRANCH_RET_CHK(ret, b_infos[ivec].expected);

    if (ivec == total_branch - 1) {
      OKNG(len_bottom > 0, "can read the end of the buffer\n");
      if (exec_path & PATH_HEAD_OVER_TAIL) {
        OKNG(len_bottom <= kmsg_buf->len - kmsg_buf->head,
             "check size of the end of the buffer\n");
        OKNG(len_top > 0, "can read the front of the buffer\n");
      }
      if (exec_path & PATH_TAIL_OVER_HEAD) {
        OKNG(len_bottom == kmsg_buf->tail - kmsg_buf->head,
             "check size of the end of the buffer\n");
        OKNG(len_top == 0, "nothing to read at the front of the buffer\n");
      }
      if (shift)
        OKNG(kmsg_buf->head == kmsg_buf->tail, "head and tail are equal\n");
    } else {
      OKNG(len_bottom + len_top == 0, "nothing to read\n");
    }
  }
  return len_bottom + len_top;
 err:
  return -EINVAL;
}

static int __ihk_os_read_kmsg_orig(struct ihk_host_linux_os_data *data,
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

/** \brief ioctl handler for reading the kernel message to the buffer */
static int __ihk_os_read_kmsg(struct ihk_host_linux_os_data *data,
                              char __user *_buf)
{
  if (g_ihk_test_mode != TEST__IHK_OS_READ_KMSG)  // Disable test code
    return __ihk_os_read_kmsg_orig(data, _buf);

  unsigned long ivec = 0;
  unsigned long total_branch = 4;
  int ret = 0;

  branch_info_t b_infos[] = {
    { -EINVAL, "invalid kmsg buffer container" },
    { -EINVAL, "invalid kmsg buffer" },
    { -ENOENT, "cannot read kmsg" },
    { 0,       "main case" },
  };

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);
    ret = 0;
    char *buf = NULL;
    int should_quit = 0;

    if (ivec == 0 || !data->kmsg_buf_container) {
      ret = -EINVAL;
      if (ivec != 0) return ret;
      goto out;
    }

    if (ivec == 1 || !data->kmsg_buf_container->kmsg_buf) {
      ret = -EINVAL;
      if (ivec != 1) return ret;
      goto out;
    }

    buf = kmalloc(IHK_KMSG_SIZE, GFP_KERNEL);
    if (!buf) {
      return -ENOMEM;
    }

    ret = read_kmsg(data->kmsg_buf_container->kmsg_buf, buf, 0);
    if (ivec == 2 || ret < 0) {
      ret = -ENOENT;
      if (ivec != 2) should_quit = 1;
      goto out;
    }

    if (copy_to_user(_buf, buf, ret)) {
      dprintf("error: copying string to user-space\n");
      ret = -EINVAL;
      should_quit = 1;
      goto out;
    }
   out:
    if (buf) {
      kfree(buf);
    }
    if (should_quit) return ret;
    int ret_cp = 0;
    if (ret > 0) {
      ret_cp = ret;
      ret = 0;
    }
    BRANCH_RET_CHK(ret, b_infos[ivec].expected);
    ret = ret_cp;
  }
  return ret;
 err:
  return -EINVAL;
}

static int __ihk_os_set_kargs_orig(struct ihk_host_linux_os_data *data,
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

/** \brief Set the kernel command-line parameter for the kernel
 *
 * This function accepts 1023 letters at most. */
static int __ihk_os_set_kargs(struct ihk_host_linux_os_data *data,
                              char __user *buf)
{
  if (g_ihk_test_mode != TEST__IHK_OS_SET_KARGS)  // Disable test code
    return __ihk_os_set_kargs_orig(data, buf);

  unsigned long ivec = 0;
  unsigned long total_branch = 2;

  branch_info_t b_infos[] = {
    { -EINVAL, "invalid handler" },
    { 0,       "main case" },
  };

  int error;
  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    char *kbuf;
    int should_quit = 0;

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
    if (ivec == 0 || !data->ops->set_kargs) {
      if (ivec != 0) should_quit = 1;
      goto out;
    }
    if (data->ops->set_kargs) {
      error = data->ops->set_kargs(data, data->priv, kbuf);
    }

   out:
    kfree(kbuf);
    if (should_quit) return error;
    BRANCH_RET_CHK(error, b_infos[ivec].expected);
  }
  return error;
 err:
  return -EINVAL;
}

void setup_monitor(struct ihk_host_linux_os_data *data)
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
  pr_debug("%s: status before checking monitor info: %d",
    __func__, ret);


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
    pr_debug("%s: i: %d, status: %d, ocounter: %ld, counter: %ld\n",
      __func__, i,
      data->monitor->cpu[i].status,
      data->monitor->cpu[i].ocounter,
      data->monitor->cpu[i].counter);

    data->monitor->cpu[i].ocounter = data->monitor->cpu[i].counter;
  }

 out:
  pr_debug("%s: status after checking monitor info: %d\n",
    __func__, ret);
  return ret;
}

static int __ihk_os_status(struct ihk_host_linux_os_data *data)
{
  /* (1) LWK sets boot_param->status to 1 in arch_init()
   * (2) LWK initializes IHK_SPADDR_MONITOR
   * (3) LWK sets boot_param->status to 2 in arch_ready()
   * (4) LWK sets boot_param->status to 3 in done_init()
   */

  return __ihk_os_query_status(data);
}

static int __ihk_os_clear_kmsg_orig(struct ihk_host_linux_os_data *data)
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

/** \brief Clear the kernel message buffer. */
static int __ihk_os_clear_kmsg(struct ihk_host_linux_os_data *data)
{
  if (g_ihk_test_mode != TEST__IHK_OS_CLEAR_KMSG)  // Disable test code
    return __ihk_os_clear_kmsg_orig(data);

  unsigned long ivec = 0;
  unsigned long total_branch = 3;

  branch_info_t b_infos[] = {
    { -EINVAL, "invalid kmsg buffer container" },
    { -EINVAL, "invalid kmsg buffer" },
    { 0,       "main case" },
  };

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    struct ihk_kmsg_buf *kmsg_buf;
    unsigned long flags;
    int ret = 0;

    if (ivec == 0 || !data->kmsg_buf_container) {
      ret = -EINVAL;
      if (ivec != 0) return ret;
      kmsg_buf = data->kmsg_buf_container->kmsg_buf;
      goto out;
    }

    if (ivec == 1 || !data->kmsg_buf_container->kmsg_buf) {
      ret = -EINVAL;
      if (ivec != 1) return ret;
      kmsg_buf = data->kmsg_buf_container->kmsg_buf;
      goto out;
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

   out:
    BRANCH_RET_CHK(ret, b_infos[ivec].expected);

    if (ivec == total_branch - 1) {
      OKNG(!kmsg_buf->head && !kmsg_buf->tail, "head and tail are reset\n");
      OKNG(!kmsg_buf->lock, "lock is reset\n");
      OKNG(strlen(kmsg_buf->str) == 0, "content is cleared\n");
    } else {
      OKNG(kmsg_buf->tail, "tail is not reset\n");
      OKNG(strlen(kmsg_buf->str) > 0, "content is not cleared\n");
    }
  }
  return 0;
 err:
  return -EINVAL;
}

static int __ihk_os_register_event_orig(struct ihk_host_linux_os_data *os, void __user *_desc)
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

static int __ihk_os_register_event(struct ihk_host_linux_os_data *os, void __user *_desc)
{
  if (g_ihk_test_mode != TEST__IHK_OS_REGISTER_EVENT)  // Disable test code
    return __ihk_os_register_event_orig(os, _desc);

  unsigned long ivec = 0;
  unsigned long total_branch = 3;

  branch_info_t b_infos[] = {
    { -ENOENT, "eventfd_fget fail" },
    { -ENOENT, "eventfd_ctx_fileget fail" },
    { 0,       "main case" },
  };

  /* save previous state */
  unsigned long flags;
  struct ihk_event *ep;
  int count_evt_prev = 0;
  spin_lock_irqsave(&os->event_list_lock, flags);
  list_for_each_entry(ep, &os->event_list, list) {
    count_evt_prev++;
  }
  spin_unlock_irqrestore(&os->event_list_lock, flags);

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    int ret = 0;
    struct ihk_os_ioctl_eventfd_desc desc;
    struct eventfd_ctx *event;
    struct file *filp;
    int count_evt_after = 0;

    if (copy_from_user(&desc, _desc, sizeof(desc))) {
      return -EFAULT;
    }

    filp = eventfd_fget(desc.fd);
    if (ivec == 0 || IS_ERR(filp)) {
      ret = -ENOENT;
      if (ivec != 0) return PTR_ERR(filp);
      goto out;
    }
    event = eventfd_ctx_fileget(filp);
    if (ivec == 1 || IS_ERR(event)) {
      ret = -ENOENT;
      if (ivec != 1) return PTR_ERR(event);
      goto out;
    }
    ep = kzalloc(sizeof(struct ihk_event), GFP_KERNEL);
    ep->event = event;
    ep->type = desc.type;
    spin_lock_irqsave(&os->event_list_lock, flags);
    list_add_tail(&ep->list, &os->event_list);
    spin_unlock_irqrestore(&os->event_list_lock, flags);

   out:
    BRANCH_RET_CHK(ret, b_infos[ivec].expected);

    /* check current state */
    spin_lock_irqsave(&os->event_list_lock, flags);
    list_for_each_entry(ep, &os->event_list, list) {
      count_evt_after++;
    }
    spin_unlock_irqrestore(&os->event_list_lock, flags);
    if (ivec == total_branch - 1) {
      OKNG(count_evt_after == count_evt_prev + 1,
           "the number of events should be increased by 1\n");
    } else {
      OKNG(count_evt_after == count_evt_prev,
           "the number of events should not be changed\n");
    }
  }
  return 0;
 err:
  return -EINVAL;
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
  int ret = 0;

  enum ihk_os_status status = __ihk_os_status(data);

  switch (status) {
  case IHK_OS_STATUS_NOT_BOOTED:
  case IHK_OS_STATUS_BOOTING:
  case IHK_OS_STATUS_BOOTED:
  case IHK_OS_STATUS_READY:
  case IHK_OS_STATUS_SHUTDOWN:
  case IHK_OS_STATUS_FAILED:
  case IHK_OS_STATUS_HUNGUP:
    pr_err("%s: error: invalid os status: %d\n",
           __func__, status);
    ret = -EINVAL;
    goto out;
  case IHK_OS_STATUS_FREEZING:
  case IHK_OS_STATUS_FROZEN:
    ret = -EBUSY;
    goto out;
  case IHK_OS_STATUS_RUNNING:
  default:
    break;
  }

  if (data->ops->freeze) {
    ret = (*data->ops->freeze)(data, data->priv);
  }

 out:
  return ret;
}

static int __ihk_os_thaw(struct ihk_host_linux_os_data *data)
{
  int ret = 0;
  enum ihk_os_status status = __ihk_os_status(data);

  switch (status) {
  case IHK_OS_STATUS_NOT_BOOTED:
  case IHK_OS_STATUS_BOOTING:
  case IHK_OS_STATUS_BOOTED:
  case IHK_OS_STATUS_READY:
  case IHK_OS_STATUS_RUNNING:
  case IHK_OS_STATUS_SHUTDOWN:
  case IHK_OS_STATUS_FAILED:
  case IHK_OS_STATUS_HUNGUP:
    pr_err("%s: error: invalid os status: %d\n",
           __func__, status);
    ret = -EINVAL;
    goto out;
  case IHK_OS_STATUS_FREEZING:
    /* wait 10 sec for frozen */
    pr_info("%s: waiting for frozen...\n", __func__);
    if (ihk_os_wait_for_status((ihk_os_t)data, IHK_OS_STATUS_FROZEN,
             0, 100) != 0) {
      pr_info("%s: warning: wait for frozen timeouted\n",
             __func__);
    }
    break;
  case IHK_OS_STATUS_FROZEN:
  default:
    break;
  }

  if (data->ops->thaw) {
    ret = (*data->ops->thaw)(data, data->priv);
  }

 out:
  return ret;
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

  return -EINVAL;
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
  case IHK_OS_GET_BUILDID:
  case IHK_OS_STATUS:
  case IHK_OS_GET_USAGE:
  case IHK_OS_GET_CPU_USAGE:
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

static int os_smp_status_target;
static int os_smp_param_status_target;
static unsigned long onesec;
static void _timer_handler_for_os_wait_status(unsigned long data);
DEFINE_TIMER(os_wait_status_timer, _timer_handler_for_os_wait_status, 0, 0);

static void _timer_handler_for_os_wait_status(unsigned long data)
{
  struct ihk_host_linux_os_data *os = (struct ihk_host_linux_os_data *) data;
  os->ops->set_smp_status(os, os_smp_status_target, os_smp_param_status_target);
}

static int __test_ihk_os_query_status(struct ihk_host_linux_os_data *os)
{
  g_ihk_test_mode = TEST_SMP_IHK_OS_QUERY_STATUS;

  unsigned long ivec = 0;
  unsigned long total_branch = 13;

  branch_info_t b_infos[] = {
    { -ENOSYS,                  "setup monitor fail" },
    { IHK_OS_STATUS_FAILED,     "cpu status is panic" },
    { IHK_OS_STATUS_BOOTING,    "os status is booting" },
    { IHK_OS_STATUS_BOOTED,     "os status is booted" },
    { IHK_OS_STATUS_HUNGUP,     "os status is hungup" },
    { IHK_OS_STATUS_SHUTDOWN,   "os status is shutdown" },
    { IHK_OS_STATUS_NOT_BOOTED, "os status is not booted" },
    /* ready/running */
    { IHK_OS_STATUS_FREEZING,   "cpu0 is freezing" },
    { IHK_OS_STATUS_FREEZING,   "cpuX is freezing" },
    { IHK_OS_STATUS_FREEZING,   "cpu0 is not frozen and cpuX is frozen" },
    { IHK_OS_STATUS_FREEZING,   "cpu0 is frozen and cpuX is not frozen" },
    { IHK_OS_STATUS_FROZEN,     "all cpus are frozen" },
    { IHK_OS_STATUS_RUNNING,    "os status is running normally" },
  };

  /* save previous state */
  int status_prev, param_status_prev;
  os->ops->get_smp_status(os, &status_prev, &param_status_prev);
  int ret, i;

  int *cpus_status_prev = NULL;
  setup_monitor(os);
  if (ivec == 0 || !os->monitor) {
    START(b_infos[ivec].name);
    ret = -ENOSYS;
    if (!os->monitor) return ret;
    BRANCH_RET_CHK(ret, b_infos[ivec].expected);
  }
  int n_cpus = os->monitor->num_processors;
  cpus_status_prev = kmalloc(n_cpus * sizeof(int), GFP_KERNEL);
  if (!cpus_status_prev) {
    return -ENOMEM;
  }
  for (i = 0; i < n_cpus; i++) {
    cpus_status_prev[i] = os->monitor->cpu[i].status;
  }

  for (ivec = 1; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    /* fake os internal status before querying */
    if (ivec == 1) {
      os->ops->set_smp_status(os, 3, 2);
      os->monitor->cpu[0].status = IHK_OS_MONITOR_PANIC;
    }
    if (ivec == 2) {
      os->ops->set_smp_status(os, 3, 0);
    }
    if (ivec == 3) {
      os->ops->set_smp_status(os, 3, 1);
    }
    if (ivec == 4) {
      os->ops->set_smp_status(os, 5, 0);
    }
    if (ivec == 5) {
      os->ops->set_smp_status(os, 4, 0);
    }
    if (ivec == 6) {
      os->ops->set_smp_status(os, 0, 0);
    }

    /* for Ready or Running */
    if (ivec == 7) {  // cpu0 is freezing
      os->ops->set_smp_status(os, 3, 2);
      os->monitor->cpu[0].status = 8;  // IHK_OS_MONITOR_KERNEL_FREEZING
    }
    if (ivec == 8) {  // cpuX is freezing
      os->ops->set_smp_status(os, 3, 2);
      os->monitor->cpu[0].status = IHK_OS_MONITOR_IDLE;
      os->monitor->cpu[1].status = IHK_OS_MONITOR_KERNEL_FREEZING;
    }
    if (ivec == 9) {  // cpuX is frozen, cpu0 is not frozen
      os->ops->set_smp_status(os, 3, 3);
      os->monitor->cpu[0].status = IHK_OS_MONITOR_IDLE;
      os->monitor->cpu[1].status = IHK_OS_MONITOR_KERNEL_FROZEN;
    }
    if (ivec == 10) {  // cpuX is not frozen, cpu0 is frozen
      os->ops->set_smp_status(os, 3, 2);
      os->monitor->cpu[0].status = IHK_OS_MONITOR_KERNEL_FROZEN;
      os->monitor->cpu[1].status = IHK_OS_MONITOR_IDLE;
    }
    if (ivec == 11) {  // all cpus are frozen
      os->ops->set_smp_status(os, 3, 2);
      for (i = 0; i < n_cpus; i++) {
        os->monitor->cpu[i].status = IHK_OS_MONITOR_KERNEL_FROZEN;
      }
    }

    /* not freezing and not frozen */
    if (ivec == 12) {
      os->ops->set_smp_status(os, 3, 3);
      for (i = 0; i < n_cpus; i++) {
        os->monitor->cpu[i].status = IHK_OS_MONITOR_IDLE;
      }
    }

    ret = __ihk_os_query_status(os);

   out:
    /* reset os and cpus status */
    os->ops->set_smp_status(os, status_prev, param_status_prev);
    for (i = 0; i < n_cpus; i++) {
      os->monitor->cpu[i].status = cpus_status_prev[i];
    }

    BRANCH_RET_CHK(ret, b_infos[ivec].expected);
  }

  ret = 0;
 rel:
  if (cpus_status_prev) kfree(cpus_status_prev);
  return ret;
 err:
  ret = -ENOSYS;
  goto rel;
}

static int __test_ihk_os_wait_for_status(struct ihk_host_linux_os_data *os)
{
  onesec = msecs_to_jiffies(1000 * 1);
  os_wait_status_timer.data = (unsigned long) os;

  g_ihk_test_mode = TEST_SMP_IHK_OS_WAIT_FOR_STATUS;

  unsigned long ivec = 0;
  unsigned long total_branch = 4;

  branch_info_t b_infos[] = {
    { -1, "sleepable" },
    { -1, "shutdown" },
    { -1, "timeout" },
    { 0,  "target status is reached" },
  };

  /* save previous state */
  int status_prev, param_status_prev;
  os->ops->get_smp_status(os, &status_prev, &param_status_prev);
  int ret;

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    ret = 0;

    if (ivec == 0) {
      ret = os->ops->wait_for_status(os, os->priv, IHK_OS_STATUS_FROZEN, 1, 10);
    }

    if (ivec == 1) {
      // force to set os status to IHK_OS_STATUS_SHUTDOWN after 2 seconds
      os_smp_status_target = 4;  // BUILTIN_OS_STATUS_SHUTDOWN
      os_smp_param_status_target = param_status_prev;
      mod_timer(&os_wait_status_timer, jiffies + 2*onesec);
      ret = os->ops->wait_for_status(os, os->priv, IHK_OS_STATUS_FROZEN, 0, 40);
    }

    if (ivec == 2) {
      os_smp_status_target = 3;  // BUILTIN_OS_STATUS_BOOTING
      os_smp_param_status_target = 1;
      os->ops->set_smp_status(os, os_smp_status_target, os_smp_param_status_target);
      ret = os->ops->wait_for_status(os, os->priv, IHK_OS_STATUS_READY, 0, 20);
    }

    if (ivec == 3) {
      // fake os status to IHK_OS_STATUS_READY
      os->ops->set_smp_status(os, 3, 2);
      // force to set os status to IHK_OS_STATUS_BOOTED after 2 seconds
      os_smp_status_target = 3;  // BUILTIN_OS_STATUS_BOOTING
      os_smp_param_status_target = 1;
      mod_timer(&os_wait_status_timer, jiffies + 2*onesec);
      ret = os->ops->wait_for_status(os, os->priv, IHK_OS_STATUS_BOOTED, 0, 40);
    }


   out:
    os->ops->set_smp_status(os, status_prev, param_status_prev);

    BRANCH_RET_CHK(ret, b_infos[ivec].expected);
  }

  return 0;
  err:
  return -EINVAL;
}

static int __ihk_os_fake_status(struct ihk_host_linux_os_data *os,
                                const char __user *arg)
{
  int ret = 0;
  os_status_req_t param;

  ret = copy_from_user(&param, arg, sizeof(param));
  if (ret) return ret;

  setup_monitor(os);
  if (!os->monitor) return -ENOSYS;
  int n_cpus = os->monitor->num_processors;
  os->ops->set_smp_status(os, param.status, param.param_status);
  if (param.cpu_status >= 0) {
    int i = 0;
    for (i = 0; i < n_cpus; i++)
      os->monitor->cpu[i].status = param.cpu_status;
  }

  return 0;
}

static int __ihk_os_get_status(struct ihk_host_linux_os_data *os,
                               char __user *res)
{
  int ret = 0;
  os_status_req_t param;

  setup_monitor(os);
  if (!os->monitor) return -ENOSYS;

  ret = os->ops->get_smp_status(os, &param.status, &param.param_status);
  if (ret) return ret;
  param.cpu_status = os->monitor->cpu[0].status;
  ret = copy_to_user(res, &param, sizeof(param));
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

/*  dprintf("IHK: ioctl request = %x, arg = %lx\n", request, arg); */

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

  case IHK_OS_WAIT_FOR_STATUS:
    ret = __test_ihk_os_wait_for_status(data);
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
    ret = __test_ihk_os_query_status(data);
    break;

  case IHK_OS_GET_STATUS:
    ret = __ihk_os_get_status(data, arg);
    break;

  case IHK_OS_FAKE_STATUS:
    ret = __ihk_os_fake_status(data, arg);
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

  case IHK_OS_PRINT_KMSG:
    ihk_host_print_os_kmsg(data);
    ret = 0;
    break;

  case IHK_OS_STATUS:
    ret = __ihk_os_status(data);
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
    return -ENOENT;
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

static int __ihk_device_create_os_init_orig(
    struct ihk_host_linux_device_data *data,
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

/** \brief Initialize a newly created OS structure */
static int __ihk_device_create_os_init(struct ihk_host_linux_device_data *data,
                                       struct ihk_host_linux_os_data **os_ptr,
                                       unsigned long arg)
{
  if (g_ihk_test_mode != TEST_IHK_DEVICE_CREATE_OS_INIT)  // Disable test code
    return __ihk_device_create_os_init_orig(data, os_ptr, arg);

  int ret = 0;
  unsigned long ivec = 0;
  unsigned long total_branch = 3;

  branch_info_t b_infos[] = {
    { -EINVAL, "create_os handler is not set" },
    { -EINVAL, "create_os error" },
    { 0,       "main case" },
  };

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    struct ihk_host_linux_os_data *os = NULL;
    struct ihk_register_os_data drv_data;
    int should_quit = 0;

    os = kzalloc(sizeof(*os), GFP_KERNEL);
    if (!os) {
      printk("ihk: kzalloc failed\n");
      ret = -ENOMEM;
      should_quit = 1;
      goto out;
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
      should_quit = 1;
      goto out;
    }

    INIT_LIST_HEAD(&os->wait_list);
    INIT_LIST_HEAD(&os->aux_call_list);
    INIT_LIST_HEAD(&os->event_list);

    if (ivec == 0 || !data->ops->create_os) {
      ret = -EINVAL;
      if (ivec != 0) should_quit = 1;
      goto out;
    }

    if (ivec != 1)
      ret = data->ops->create_os(data, data->priv, arg, os, &drv_data);
    if (ivec == 1 || ret) {
      if (ivec != 1) {
        printk("ihk: create_os failed (%d)\n", ret);
        should_quit = 1;
      }
      ret = -EINVAL;
      goto out;
    }

    os->name = drv_data.name;
    os->flag = IHK_OS_FLAG_SHARABLE;
    os->ops = drv_data.ops;
    os->priv = drv_data.priv;
    os->dev_data = data;

    cdev_init(&os->cdev, &mcos_cdev_ops);

    *os_ptr = os;

    ret = 0;

  out:
    if (ivec < total_branch-1 && os) {
      kfree(os->regular_channels);
      kfree(os);
    }

    BRANCH_RET_CHK(ret, b_infos[ivec].expected);

    if (ivec == total_branch - 1) {
      OKNG(*os_ptr != NULL, "output valid pointer\n");
    } else {
      OKNG(*os_ptr == NULL, "result pointer is null\n");
    }

    if (should_quit) return ret;
  }

  return 0;
 err:
  return -EINVAL;
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

static int __ihk_device_create_os_orig(struct ihk_host_linux_device_data *data,
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

static void _remove_fake_conts(void)
{
  struct ihk_kmsg_buf_container *cont, *tmp;
  unsigned long flags;
  spin_lock_irqsave(&ihk_kmsg_bufs_lock, flags);
  list_for_each_entry_safe(cont, tmp, &ihk_kmsg_bufs, list) {
    if (cont->os_index >= 0) continue;
    delete_kmsg_buf(cont);
  }
  spin_unlock_irqrestore(&ihk_kmsg_bufs_lock, flags);
}

/** \brief Create a OS file in the kernel
 *
 * @return minor number */
static int __ihk_device_create_os(struct ihk_host_linux_device_data *data,
                                  unsigned long arg)
{
  if (g_ihk_test_mode != TEST_IHK_DEVICE_CREATE_OS)  // Disable test code
    return __ihk_device_create_os_orig(data, arg);

  unsigned long ivec = 0;
  unsigned long total_branch = 8;

  branch_info_t b_infos[] = {
    { 0,       "none free slot" },
    { -ENOMEM, "exceed OS_MAX_MINOR" },
    { -EINVAL, "cannot create and init os" },
    { 0,       "ihk_kmsg_bufs is full" },
    { 0,       "ihk_kmsg_bufs has at least 1 seat" },
    { -ENOMEM, "cdev_add fail" },
    { -ENOMEM, "device_create fail" },
    { 0,       "main case" },
  };

  int minor = -1;
  int os_max_minor_prev = os_max_minor;
  int nbufs_prev = 0, nbufs_after = 0;
  struct ihk_kmsg_buf_container *cont, *cont_it, *cont_first = NULL;
  list_for_each_entry(cont_it, &ihk_kmsg_bufs, list) {
    nbufs_prev++;
  }

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    unsigned long flags;
    int kmsg_buf_size;
    unsigned int kmsg_buf_order;
    struct page *kmsg_buf_pages = NULL;
    struct ihk_kmsg_buf *kmsg_buf = NULL;
    struct ihk_host_linux_os_data *os = NULL;
    cont = NULL;
    int nbufs = 0;
    int ret = 0, i;
    int should_quit = 0;
    os = NULL;
    minor = -1;

    os_max_minor = os_max_minor_prev;
    if (ivec == 0) os_max_minor = 3;  // fake last os minor

    /* first check if there is any free slot */
    spin_lock_irqsave(&os_data_lock, flags);
    for (i = 0; i < os_max_minor; i++) {
      if (ivec == 0) continue;  // there is none free slot
      if (!os_data[i]) {
        break;
      }
    }
    if (ivec <= 1 || i == os_max_minor) {
      if (ivec == 1 || os_max_minor >= OS_MAX_MINOR) {
        spin_unlock_irqrestore(&os_data_lock, flags);
        ret = -ENOMEM;
        if (ivec != 1) {
          printk("ihk: os_max_minor exceeds.\n");
          return ret;
        }
        goto out;
      }
      // ivec = 0 goes here
      os_max_minor++;
    }

    minor = i;
    os_data[minor] = OS_DATA_INVALID;

    spin_unlock_irqrestore(&os_data_lock, flags);

    if (ivec != 2)
      ret = __ihk_device_create_os_init(data, &os, arg);
    if (ivec == 2 || ret != 0) {
      os_data[minor] = NULL;
      if (ivec != 2) return ret;
      ret = -EINVAL;
      goto out;
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
      should_quit = 1;
      goto err;
    }

    /* Initialize kmsg_buf */
    kmsg_buf = (struct ihk_kmsg_buf *)pfn_to_kaddr(page_to_pfn(kmsg_buf_pages));
    kmsg_buf->tail = 0;
    kmsg_buf->len = sizeof(kmsg_buf->str);
    kmsg_buf->head = 0;
    kmsg_buf->lock = 0;
    memset(kmsg_buf->str, 0, sizeof(kmsg_buf->str));

    if (ivec == total_branch - 1)
      dkprintf("%s: kmsg_buf=%p\n", __FUNCTION__, kmsg_buf);

    /* assume that the kmsg bufs is full,
     * so we have to remove the oldest for adding the new one */
    if (ivec == 3) {
      // firstly we must make a full kmsg bufs
      for (i = 0; i < IHK_MAX_NUM_KMSG_BUFS-nbufs_prev; i++) {
        struct page *bufpage = alloc_pages(GFP_KERNEL | __GFP_ZERO, kmsg_buf_order);
        if (!bufpage) {
          ret = -ENOMEM;
          should_quit = 1;
          goto err;
        }
        struct ihk_kmsg_buf *buf = (struct ihk_kmsg_buf *)pfn_to_kaddr(page_to_pfn(bufpage));
        cont_it = kmalloc(sizeof(struct ihk_kmsg_buf_container), GFP_KERNEL);
        if (!cont_it) {
          ret = -ENOMEM;
          should_quit = 1;
          goto err;
        }
        cont_it->os_index = -1;
        cont_it->kmsg_buf = buf;
        cont_it->order = kmsg_buf_order;
        spin_lock_irqsave(&ihk_kmsg_bufs_lock, flags);
        list_add_tail(&cont_it->list, &ihk_kmsg_bufs);
        spin_unlock_irqrestore(&ihk_kmsg_bufs_lock, flags);
      }
    }

    /* Release stray kmsg_bufs */
    spin_lock_irqsave(&ihk_kmsg_bufs_lock, flags);
    list_for_each_entry(cont_it, &ihk_kmsg_bufs, list) {
      nbufs++;
    }

    if (ivec == total_branch - 1)
      dkprintf("%s: number of kmsg_buf=%d\n", __FUNCTION__, nbufs);

    if (ivec == 4 || (ivec != 3 && nbufs <= (IHK_MAX_NUM_KMSG_BUFS - 1))) {
      if (nbufs > IHK_MAX_NUM_KMSG_BUFS - 1) {  // all seats are busy
        /* should remove one exist container to reserve seat for the new one */
        cont_first = list_first_entry(&ihk_kmsg_bufs,
                                      struct ihk_kmsg_buf_container, list);
        list_del(&cont_first->list);
      }
    } else
    /* with ivec = 3, we will definitely go inside this for-loop
     * because we have manually created a full kmsg bufs before */
    for (i = 0; i < nbufs - (IHK_MAX_NUM_KMSG_BUFS - 1); i++) {
      cont = list_first_entry(&ihk_kmsg_bufs,
                              struct ihk_kmsg_buf_container, list);
      delete_kmsg_buf(cont);
      if (ivec != 3)
        ekprintf("%s: Warning: stray kmsg_buf %p freed\n", __FUNCTION__, cont);
    }
    spin_unlock_irqrestore(&ihk_kmsg_bufs_lock, flags);

    /* Insert it into the list */
    cont = kmalloc(sizeof(struct ihk_kmsg_buf_container), GFP_KERNEL);
    if (!cont) {
      pr_info("IHK: Cannot allocate kmsg buffer container\n");
      __free_pages(kmsg_buf_pages, kmsg_buf_order);
      ret = -ENOMEM;
      should_quit = 1;
      goto err;
    }
    cont->os_index = minor;
    cont->kmsg_buf = kmsg_buf;
    atomic_set(&cont->count, 0);
    cont->order = kmsg_buf_order;
    spin_lock_irqsave(&ihk_kmsg_bufs_lock, flags);
    list_add_tail(&cont->list, &ihk_kmsg_bufs);
    spin_unlock_irqrestore(&ihk_kmsg_bufs_lock, flags);
    if (ivec == total_branch - 1)
      dkprintf("%s: kmsg_buf %p added\n", __FUNCTION__, cont);

    os->cdev.owner = THIS_MODULE;
    os->dev_num = mcos_dev_num + minor;

    if (ivec != 5)
      ret = cdev_add(&os->cdev, os->dev_num, 1);
    if (ivec == 5 || ret < 0) {
      ret = -ENOMEM;
      if (ivec != 5) {
        printk("ihk: cdev_add failed (%d)\n", ret);
        should_quit = 1;
        goto err;
      }
      goto out;
    }

    /* set os_data[minor] before creating device to avoid creating
     * the device before it's useable
     */
    os_data[minor] = os;
    os->minor = minor;

    if (ivec != 6)
      os->lindev = device_create(mcos_class, NULL, os->dev_num, NULL,
                                 OS_DEV_NAME "%d", minor);
    if (ivec == 6 || IS_ERR(os->lindev)) {
      ret = -ENOMEM;
      if (ivec != 6) {
        printk("ihk: device_create failed.\n");
        should_quit = 1;
        goto err;
      }
      goto out;
    }

    ret = 0;

   out:
    if (ivec != 0 && ivec != 3 && ivec != total_branch-1) {
      spin_lock_irqsave(&ihk_kmsg_bufs_lock, flags);
      delete_kmsg_buf(cont); cont = NULL;
      spin_unlock_irqrestore(&ihk_kmsg_bufs_lock, flags);
    }

    if (should_quit) goto err;
    should_quit = 1;

    if (ivec == 3) _remove_fake_conts();  // remove dummies

    nbufs_after = 0;
    spin_lock_irqsave(&ihk_kmsg_bufs_lock, flags);
    list_for_each_entry(cont_it, &ihk_kmsg_bufs, list) {
      nbufs_after++;
    }
    spin_unlock_irqrestore(&ihk_kmsg_bufs_lock, flags);

    BRANCH_RET_CHK(ret, b_infos[ivec].expected);

    if (ivec == 0) {
      OKNG(minor == 3, "checking minor value\n");
      OKNG(os_max_minor == 4, "os_max_minor should be increased by 1\n");
    }

    if (ivec != 0 && ivec != 3 && ivec != total_branch-1) {
      OKNG(nbufs_prev == nbufs_after,
           "the number of kmsg bufs should be unchanged\n");
    } else {
      if (nbufs_prev < IHK_MAX_NUM_KMSG_BUFS) {
        OKNG(nbufs_prev == nbufs_after - 1,
             "the number of kmsg bufs should be increased by 1\n");
      } else {
        OKNG(nbufs_after == IHK_MAX_NUM_KMSG_BUFS,
             "the number of kmsg bufs should not exceed %d\n", IHK_MAX_NUM_KMSG_BUFS);
      }
      OKNG(os_max_minor <= OS_MAX_MINOR, "checking max minor constrain\n");
      OKNG(minor >= 0 && minor < os_max_minor, "checking minor constrain\n");
      OKNG(os != NULL && os_data[minor] == os, "new os_data should be added\n");
    }
    should_quit = 0;

   err:
    _remove_fake_conts();
    if (ivec != total_branch - 1 || should_quit) {
      spin_lock_irqsave(&ihk_kmsg_bufs_lock, flags);
      delete_kmsg_buf(cont);
      if (ivec == 4 && cont_first)
        list_add_tail(&cont_first->list, &ihk_kmsg_bufs);
      spin_unlock_irqrestore(&ihk_kmsg_bufs_lock, flags);
      if (minor >= 0)  {
        os_data[minor] = NULL;
        __ihk_device_destroy_os(data, os);
      }
      if (should_quit) return (ret < 0)? ret : -EINVAL;
    }
  }

  return minor;
}

static int __ihk_device_destroy_os_orig(struct ihk_host_linux_device_data *data,
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
    pr_err("%s: error: refcount != 0 (%d)\n",
           __func__, atomic_read(&os->refcount));
    return -EBUSY;
  }

  __ihk_os_shutdown(os, FLAG_IHK_OS_SHUTDOWN_FORCE);

  if (data->ops->destroy_os) {
    ret = data->ops->destroy_os(data, data->priv, os, os->priv);
    if (ret) {
      pr_err("%s: error: destroy_os: ret: %d\n",
             __func__, ret);
      return -EINVAL;
    }
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

static int _check_os_instance_exist(int minor)
{
  char path[200];
  sprintf(path, "/dev/mcos%d", minor);
  return fs_entry_exist(path);
}

static struct ihk_kmsg_buf_container *_find_cont(int minor)
{
  struct ihk_kmsg_buf_container *cont, *ret = NULL;
  unsigned long flags;
  spin_lock_irqsave(&ihk_kmsg_bufs_lock, flags);
  list_for_each_entry_reverse(cont, &ihk_kmsg_bufs, list) {
    if (cont->os_index == minor) {
      ret = cont;
      break;
    }
  }
  spin_unlock_irqrestore(&ihk_kmsg_bufs_lock, flags);
  return ret;
}

/** \brief Destroy an OS structure, and also the corresponding device file */
static int __ihk_device_destroy_os(struct ihk_host_linux_device_data *data,
                                   struct ihk_host_linux_os_data *os)
{
  if (g_ihk_test_mode != TEST_IHK_DEVICE_DESTROY_OS)  // Disable test code
    return __ihk_device_destroy_os_orig(data, os);

  dkprintf("__ihk_device_destroy_os (%p, %p)\n", data, os);

  int ret = 0;
  unsigned long ivec = 0;
  unsigned long total_branch = 6;

  branch_info_t b_infos[] = {
    { -EINVAL, "invalid parameter" },
    { -EBUSY,  "os instance is busy" },
    { -EINVAL, "shutdown os fail" },
    { -EINVAL, "destroy os handler is not set" },
    { -EINVAL, "destroy os fail" },
    { 0,       "main case" }
  };

  enum ihk_os_status os_status_prev = __ihk_os_status(os);
  enum ihk_os_status os_status_after;
  struct ihk_event *ep;
  int count_evt_prev = 0;
  list_for_each_entry(ep, &os->event_list, list) {
    count_evt_prev++;
  }

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    struct ihk_kmsg_buf_container *cont = NULL;
    int count_evt_after = 0;
    ret = 0;

    if (ivec == 0 || !os || os == OS_DATA_INVALID
        || !data || data == DEV_DATA_INVALID || os->dev_data != data) {
      ret = -EINVAL;
      if (ivec != 0) {
        dkprintf("%s: pointer invalid\n", __FUNCTION__);
        return ret;
      }
      goto out;
    }

    if (ivec == 1 || atomic_read(&os->refcount) > 0) {
      ret = -EBUSY;
      if (ivec != 1) {
        pr_err("%s: error: refcount != 0 (%d)\n",
               __func__, atomic_read(&os->refcount));
        return ret;
      }
      goto out;
    }

    if (ivec > 4)
      ret = __ihk_os_shutdown(os, FLAG_IHK_OS_SHUTDOWN_FORCE);
    if (ivec == 2 || ret) {
      ret = -EINVAL;
      if (ivec != 2) return ret;
      goto out;
    }

    if (ivec == 3 || !data->ops->destroy_os) {
      ret = -EINVAL;
      if (ivec != 3) return ret;
      goto out;
    }

    if (ivec > 4)
      ret = data->ops->destroy_os(data, data->priv, os, os->priv);
    if (ivec == 4 || ret) {
      ret = -EINVAL;
      if (ivec != 4) {
        pr_err("%s: error: destroy_os: ret: %d\n",
              __func__, ret);
        return ret;
      }
      goto out;
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

    ret = 0;

   out:
    BRANCH_RET_CHK(ret, b_infos[ivec].expected);

    os_status_after = __ihk_os_status(os);
    unsigned int refcnt = atomic_read(&os->cdev.kobj.kref.refcount.refs);
    list_for_each_entry(ep, &os->event_list, list) {
      count_evt_after++;
    }
    cont = _find_cont(os->minor);

    if (ivec == total_branch - 1) {
      OKNG(os_status_after == IHK_OS_STATUS_NOT_BOOTED,
           "os status should be updated\n");
      OKNG(refcnt == 0, "ref count of cdev device should equal 0\n");
      int exist = _check_os_instance_exist(os->minor);
      OKNG(!exist, "os device entry should be removed\n");
      OKNG(os_data[os->minor] == NULL, "os_data should be null\n");
      OKNG(count_evt_after == 0, "events list should be empty\n");
      OKNG(!cont, "kmsg buf container is removed\n");
    } else {
      OKNG(os_status_prev == os_status_after, "os status should not be changed\n");
      OKNG(refcnt > 0, "ref count of cdev device should be greater than 0\n");
      int exist = _check_os_instance_exist(os->minor);
      OKNG(exist, "os device entry should exist\n");
      OKNG(os_data[os->minor] != NULL, "os_data should not be null\n");
      OKNG(count_evt_after == count_evt_prev, "events list should not be changed\n");
      OKNG(cont, "kmsg buf container is still on the list\n");
    }
  }

  kfree(os);
  return 0;
 err:
  return ret;
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
static int __ihk_device_reserve_cpu_orig(struct ihk_host_linux_device_data *data,
    unsigned long arg)
{
  if (!data->ops || !data->ops->reserve_cpu)
    return -1;

  return data->ops->reserve_cpu(data, arg);
}

static int __ihk_device_reserve_cpu(struct ihk_host_linux_device_data *data,
    unsigned long arg)
{
  if (g_ihk_test_mode != TEST_IHK_DEVICE_RESERVE_CPU)  // Disable test code
    return __ihk_device_reserve_cpu_orig(data, arg);

  unsigned long ivec = 0;
  unsigned long total_branch = 2;
  int ret;

  branch_info_t b_infos[] = {
    { -1, "invalid handler" },
    { 0,  "main case" },
  };

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    if (ivec == 0 || (!data->ops || !data->ops->reserve_cpu)) {
      ret = -1;
      if (ivec == 0) goto out;
      goto err;
    }

    ret = data->ops->reserve_cpu(data, arg);

   out:
    BRANCH_RET_CHK(ret, b_infos[ivec].expected);
  }

  return ret;
 err:
  return -1;
}

ihk_test_mode_t g_ihk_test_mode;

/** \brief Set Test mode for IHK API */
static int __ihk_device_set_test_mode(struct ihk_host_linux_device_data *data,
    unsigned long arg)
{
  if (!data->ops || !data->ops->set_test_mode)
    return -1;

  int mode;
  if (copy_from_user(&mode, (void *)arg, sizeof(mode))) {
    printk("%s: error: copying test mode\n", __FUNCTION__);
    return -1;
  }

  int ret = data->ops->set_test_mode(data, arg);
  if (ret) return -1;

  struct ihk_os_notifier *_ion;
  list_for_each_entry(_ion, &ihk_os_notifiers, nlist) {
    if (_ion->ops && _ion->ops->set_test_mode)
      _ion->ops->set_test_mode(mode);
  }

  g_ihk_test_mode = mode;
  return 0;
}

static int __ihk_device_release_cpu_orig(struct ihk_host_linux_device_data *data,
    unsigned long arg)
{
  if (!data->ops || !data->ops->release_cpu)
    return -1;

  return data->ops->release_cpu(data, arg);
}

/** \brief Release CPU cores */
static int __ihk_device_release_cpu(struct ihk_host_linux_device_data *data,
    unsigned long arg)
{
  if (g_ihk_test_mode != TEST_IHK_DEVICE_RELEASE_CPU)  // Disable test code
    return __ihk_device_release_cpu_orig(data, arg);

  unsigned long ivec = 0;
  unsigned long total_branch = 2;
  int ret = 0;

  branch_info_t b_infos[] = {
    { -1, "invalid handler" },
    { 0,  "main case" },
  };

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    if (ivec == 0 || (!data->ops || !data->ops->release_cpu)) {
      ret = -1;
      if (ivec == 0) goto out;
      goto err;
    }

    ret = data->ops->release_cpu(data, arg);

   out:
    BRANCH_RET_CHK(ret, b_infos[ivec].expected);
  }

 err:
  return ret;
}

/** \brief Reserve memory */
static int __ihk_device_reserve_mem_orig(struct ihk_host_linux_device_data *data,
    unsigned long arg)
{
  if (!data->ops || !data->ops->reserve_mem)
    return -1;

  return data->ops->reserve_mem(data, arg);
}

static int __ihk_device_reserve_mem(struct ihk_host_linux_device_data *data,
    unsigned long arg)
{
  if (g_ihk_test_mode != TEST_IHK_DEVICE_RESERVE_MEM)  // Disable test code
    return __ihk_device_reserve_mem_orig(data, arg);

  unsigned long ivec = 0;
  unsigned long total_branch = 2;
  int ret = 0;

  branch_info_t b_infos[] = {
    { -1, "invalid handler" },
    { 0,  "main case" },
  };

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    if (ivec == 0 || (!data->ops || !data->ops->reserve_mem)) {
      ret = -1;
      if (ivec == 0) goto out;
      goto err;
    }

    ret = data->ops->reserve_mem(data, arg);

   out:
    BRANCH_RET_CHK(ret, b_infos[ivec].expected);
  }

 err:
  return ret;
}

static int __ihk_device_release_mem_orig(struct ihk_host_linux_device_data *data,
    unsigned long arg)
{
  if (!data->ops || !data->ops->release_mem)
    return -1;

  return data->ops->release_mem(data, arg);
}

/** \brief Release memory */
static int __ihk_device_release_mem(struct ihk_host_linux_device_data *data,
    unsigned long arg)
{
  if (g_ihk_test_mode != TEST_IHK_DEVICE_RELEASE_MEM)  // Disable test code
    return __ihk_device_release_mem_orig(data, arg);

  unsigned long ivec = 0;
  unsigned long total_branch = 2;
  int ret = 0;

  branch_info_t b_infos[] = {
    { -1, "invalid handler" },
    { 0,  "main case" },
  };

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    if (ivec == 0 || (!data->ops || !data->ops->release_mem)) {
      ret = -1;
      if (ivec == 0) goto out;
      goto err;
    }

    ret = data->ops->release_mem(data, arg);

   out:
    BRANCH_RET_CHK(ret, b_infos[ivec].expected);
  }

 err:
  return ret;
}

static int __ihk_device_release_mem_partially_orig(struct ihk_host_linux_device_data *data,
                unsigned long arg)
{
  if (!data->ops || !data->ops->release_mem_partially)
    return -1;

  return data->ops->release_mem_partially(data, arg);
}

/** \brief Release memory */
static int __ihk_device_release_mem_partially(struct ihk_host_linux_device_data *data,
                unsigned long arg)
{
  if (g_ihk_test_mode != TEST_IHK_DEVICE_RELEASE_MEM_PARTIALLY)  // Disable test code
    return __ihk_device_release_mem_partially_orig(data, arg);

  unsigned long ivec = 0;
  unsigned long total_branch = 2;
  int ret = 0;

  branch_info_t b_infos[] = {
    { -1, "invalid handler" },
    { 0,  "main case" },
  };

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    if (ivec == 0 || (!data->ops || !data->ops->release_mem_partially)) {
      ret = -1;
      if (ivec == 0) goto out;
      goto err;
    }

    ret = data->ops->release_mem_partially(data, arg);

   out:
    BRANCH_RET_CHK(ret, b_infos[ivec].expected);
  }

 err:
  return ret;
}

static int __ihk_device_get_num_cpus_orig(struct ihk_host_linux_device_data *data)
{
  if (!data->ops || !data->ops->get_num_cpus)
    return -1;

  return data->ops->get_num_cpus(data);
}

/** \brief Query number of CPU cores */
static int __ihk_device_get_num_cpus(struct ihk_host_linux_device_data *data)
{
  if (g_ihk_test_mode != TEST_IHK_DEVICE_GET_NUM_CPUS)  // Disable test code
    return __ihk_device_get_num_cpus_orig(data);

  unsigned long ivec = 0;
  unsigned long total_branch = 2;
  int ret = 0;

  branch_info_t b_infos[] = {
    { -1, "invalid handler" },
    { 0,  "main case" },
  };

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    if (ivec == 0 || (!data->ops || !data->ops->get_num_cpus)) {
      ret = -1;
      if (ivec == 0) goto out;
      goto err;
    }

    int num_cpus = data->ops->get_num_cpus(data);
    ret = (num_cpus > 0)? 0 : ret;

   out:
    BRANCH_RET_CHK(ret, b_infos[ivec].expected);
    ret = num_cpus;
  }

 err:
  return ret;
}

static int __ihk_device_query_cpu_orig(struct ihk_host_linux_device_data *data,
    unsigned long arg)
{
  if (!data->ops || !data->ops->query_cpu)
    return -1;

  return data->ops->query_cpu(data, arg);
}

/** \brief Query CPU cores */
static int __ihk_device_query_cpu(struct ihk_host_linux_device_data *data,
    unsigned long arg)
{
  if (g_ihk_test_mode != TEST_IHK_DEVICE_QUERY_CPU)  // Disable test code
    return __ihk_device_query_cpu_orig(data, arg);

  unsigned long ivec = 0;
  unsigned long total_branch = 2;
  int ret = 0;

  branch_info_t b_infos[] = {
    { -1, "invalid handler" },
    { 0,  "main case" },
  };

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    if (ivec == 0 || (!data->ops || !data->ops->query_cpu)) {
      ret = -1;
      if (ivec == 0) goto out;
      goto err;
    }

    ret = data->ops->query_cpu(data, arg);

   out:
    BRANCH_RET_CHK(ret, b_infos[ivec].expected);
  }

 err:
  return ret;
}

static int __ihk_device_query_mem_orig(struct ihk_host_linux_device_data *data,
    unsigned long arg)
{
  if (!data->ops || !data->ops->query_mem)
    return -1;

  return data->ops->query_mem(data, arg);
}

/** \brief Query memory */
static int __ihk_device_query_mem(struct ihk_host_linux_device_data *data,
    unsigned long arg)
{
  if (g_ihk_test_mode != TEST_IHK_DEVICE_QUERY_MEM)  // Disable test code
    return __ihk_device_query_mem_orig(data, arg);

  unsigned long ivec = 0;
  unsigned long total_branch = 2;
  int ret = 0;

  branch_info_t b_infos[] = {
    { -1, "invalid handler" },
    { 0,  "main case" },
  };

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    if (ivec == 0 || (!data->ops || !data->ops->query_mem)) {
      ret = -1;
      if (ivec == 0) goto out;
      goto err;
    }

    ret = data->ops->query_mem(data, arg);

   out:
    BRANCH_RET_CHK(ret, b_infos[ivec].expected);
  }

 err:
  return ret;
}

static long ihk_host_device_ioctl_orig(struct file *file, unsigned int request,
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

  case IHK_DEVICE_SET_TEST_MODE:
    ret = __ihk_device_set_test_mode(data, arg);
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

/** \brief ioctl handler for the device file */
static long ihk_host_device_ioctl(struct file *file, unsigned int request,
                                  unsigned long arg)
{
  if (g_ihk_test_mode != TEST_IHK_HOST_DEVICE_IOCTL)  // Disable test code
    return ihk_host_device_ioctl_orig(file, request, arg);

  unsigned long ivec = 0;
  unsigned long total_branch = 2;

  branch_info_t b_infos[] = {
    { -EINVAL, "invalid request" },
    { 0,       "main case" },
  };

  int ret = -EINVAL;
  unsigned int request_prev = request;

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    ret = -EINVAL;
    struct ihk_host_linux_device_data *data;
    request = request_prev;
    data = file->private_data;

    // fake invalid request
    if (ivec == 0) request = IHK_DEVICE_DEBUG_END + 1;

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

    case IHK_DEVICE_SET_TEST_MODE:
      ret = __ihk_device_set_test_mode(data, arg);
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

    int tmp_ret = ret;
    if (ret > 0) ret = 0;
   out:
    BRANCH_RET_CHK(ret, b_infos[ivec].expected);

    ret = tmp_ret;
  }

 err:
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

int ihk_os_get_num_handlers(ihk_os_t os)
{
  struct ihk_host_linux_os_data *_os = os;
  return _os->ops->get_num_handlers(os, _os->priv);
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

unsigned long ihk_device_map_memory_orig(ihk_device_t dev, unsigned long pa,
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

unsigned long ihk_device_map_memory(ihk_device_t dev, unsigned long pa,
                                    unsigned long size)
{
  if (g_ihk_test_mode != TEST_IHK_DEVICE_MAP_MEMORY)  // Disable test code
    return ihk_device_map_memory_orig(dev, pa, size);

  unsigned long ivec = 0;
  unsigned long total_branch = 2;

  branch_info_t b_infos[] = {
    { 0, "cannot map memory" },
    { 0, "main case" },
  };

  unsigned long ret;

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    /* XXX: PAGE_SIZE should be device-specific */
    unsigned long st, ed, offset, r;

    offset = pa & (PAGE_SIZE - 1);
    st = pa & PAGE_MASK;
    ed = (pa + size + PAGE_SIZE - 1) & PAGE_MASK;

    if (ivec > 0)
      r = __ihk_device_map_memory(dev, st, ed - st);
    if (ivec == 0 || (long) r <= 0) {
      ret = 0;
      if (ivec != 0) return r;
      goto out;
    }

    ret = r + offset;
   out:
    if (ivec == total_branch - 1) {
      OKNG(ret > 0, "mapped memory should be valid\n");
    } else {
      OKNG(ret <= 0, "mapped memory is invalid\n");
    }
  }
  return ret;
 err:
  return -EINVAL;
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

ihk_os_t ihk_host_find_os_orig(int index, ihk_device_t dev)
{
  if (!os_data[index] || os_data[index] == DEV_DATA_INVALID) {
    return NULL;
  } else {
    if (!dev || os_data[index]->dev_data == dev) {
      return os_data[index];
    } else {
      return NULL;
    }
  }
}

ihk_os_t ihk_host_find_os(int index, ihk_device_t dev)
{
  if (g_ihk_test_mode != TEST_IHK_HOST_FIND_OS)  // Disable test code
    return ihk_host_find_os_orig(index, dev);

  unsigned long ivec = 0;
  unsigned long total_branch = 4;

  branch_info_t b_infos[] = {
    { 0, "invalid index" },
    { 0, "instance at specified index is not set" },
    { 0, "dev data mismatch" },
    { 0, "main case" },
  };

  ihk_os_t ret = NULL;

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    if (ivec == 0 || (index < 0 || index >= OS_MAX_MINOR)) {
      ret = NULL;
      if (ivec != 0) return ret;
      goto out;
    }

    if (ivec == 1 ||
        (ivec > 3 && (!os_data[index] || os_data[index] == DEV_DATA_INVALID))) {
      ret = NULL;
      if (ivec != 1) return ret;
    } else {
      if (ivec == 3 || (ivec != 2 && (!dev || os_data[index]->dev_data == dev))) {
        ret = os_data[index];
      } else {  // ivec = 2 goes here
        ret = NULL;
        if (ivec != 2) return ret;
      }
    }

   out:
    if (ivec == total_branch - 1) {
      OKNG(ret, "found a valid os instance\n");
    } else {
      OKNG(!ret, "not found the specified os instance\n");
    }
  }
  return ret;
 err:
  return NULL;
}

int ihk_host_validate_os(ihk_os_t os)
{
  int i;
  int found = 0;

  for (i = 0; i < os_max_minor; i++) {
    if (os == os_data[i] && os_data[i] &&
        os_data[i] != (void *)-1) {
      found = 1;
    }
  }

  return found ? 0 : -EINVAL;
}

void ihk_host_print_os_kmsg_orig(ihk_os_t os)
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

void ihk_host_print_os_kmsg(ihk_os_t os)
{
  if (g_ihk_test_mode != TEST_IHK_HOST_PRINT_OS_KMSG)  // Disable test code
    return ihk_host_print_os_kmsg_orig(os);

  unsigned long ivec = 0;
  unsigned long total_branch = 4;

  branch_info_t b_infos[] = {
    { 0, "invalid os instance" },
    { 0, "kmsg buffer is not available" },
    { 0, "kmsg buffer is empty" },
    { 0, "main case" },
  };

  struct ihk_host_linux_os_data *data = (struct ihk_host_linux_os_data *)os;

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    int nread;
    char *buf;
    char *lines, *line;
    int count_line = 0;
    int should_quit = 0;

    buf = kmalloc(IHK_KMSG_SIZE, GFP_KERNEL);
    if (!buf) {
      return;
    }
    if (ivec == 0 || (!os || ihk_host_validate_os(os))) {
      if (ivec != 0) should_quit = 1;
      goto out;
    }

    nread = read_kmsg(data->kmsg_buf_container->kmsg_buf, buf, 0);
    if (ivec == 1 || nread < 0) {
      if (ivec != 1) {
        printk("%s: kmsg_buf is not available\n", __FUNCTION__);
        should_quit = 1;
      }
      goto out;
    }
    if (ivec == 2 || nread == 0) {
      if (ivec != 2) {
        printk("%s: kmsg buffer is empty\n", __FUNCTION__);
        should_quit = 1;
      }
      goto out;
    }

    /* Print line-by-line */
    lines = buf;
    line = strsep(&lines, "\n");
    while (line) {
      printk("%s\n", line);
      line = strsep(&lines, "\n");
      count_line++;
    }

   out:
    if (buf) {
      kfree(buf);
    }
    if (should_quit) return;

    if (ivec == total_branch - 1) {
      OKNG(count_line, "kmsg buffer is printed to the kernel log\n");
    } else {
      OKNG(count_line == 0, "kmsg buffer is not printed to the kernel log\n");
    }
  }
 err:
 return;
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

struct list_head *ihk_os_get_ioctl_handlers(ihk_os_t ihk_os)
{
  struct ihk_host_linux_os_data *os = ihk_os;
  return &os->aux_call_list;
}

struct ihk_os_kernel_call_handler *ihk_os_get_kernel_call_handlers(
    ihk_os_t ihk_os)
{
  struct ihk_host_linux_os_data *os = ihk_os;
  return os->kernel_handlers;
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
  int ret;
  struct ihk_host_linux_os_data *os = ihk_os;

  ret = ihk_host_validate_os((ihk_os_t)os);
  if (ret) {
    pr_err("%s: error: invalid os: %lx\n",
           __func__, (unsigned long)os);
    return ret;
  }

  if (!os || !os->kernel_handlers ||
      !os->kernel_handlers->read_cpu_register) {
    return -EINVAL;
  }

  return os->kernel_handlers->read_cpu_register(ihk_os, cpu, desc);
}

int ihk_os_write_cpu_register(ihk_os_t ihk_os, int cpu,
    struct ihk_os_cpu_register *desc)
{
  int ret;
  struct ihk_host_linux_os_data *os = ihk_os;

  ret = ihk_host_validate_os((ihk_os_t)os);
  if (ret) {
    pr_err("%s: error: invalid os: %lx\n",
           __func__, (unsigned long)os);
    return ret;
  }

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
 *    ihk_os (OUTPUT):  LWK OS instance of system call offload origin
 *    cpu (OUTPUT):  CPU number of the system call offload origin
 *  Return value:
 *    0:    Caller is performing system call offload
 *    -EINVAL:    Caller isn't performing system call offload
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


int ihk_os_register_user_call_handlers_orig(ihk_os_t ihk_os,
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

int ihk_os_register_user_call_handlers(ihk_os_t ihk_os,
                                       struct ihk_os_user_call *clist)
{
  if (g_ihk_test_mode != TEST_IHK_OS_REGISTER_USER_CALL_HANDLERS)  // Disable test code
    return ihk_os_register_user_call_handlers_orig(ihk_os, clist);

  unsigned long ivec = 0;
  unsigned long total_branch = 4;

  branch_info_t b_infos[] = {
    { -EINVAL, "invalid parameter" },
    { -EINVAL, "num_handlers is zero" },
    { -EINVAL, "invalid handler id" },
    { 0,       "main case" },
  };

  int i;
  unsigned long flags;
  struct ihk_host_linux_os_data *os = ihk_os;
  int count_list_prev = 0;
  struct ihk_os_user_call *c_it;
  spin_lock_irqsave(&os->lock, flags);
  list_for_each_entry(c_it, &os->aux_call_list, list) {
    count_list_prev++;
  }
  spin_unlock_irqrestore(&os->lock, flags);

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    int ret = 0;
    int count_list_after = 0;

    if (ivec == 0 || (ihk_host_validate_os(ihk_os) || !clist)) {
      ret = -EINVAL;
      if (ivec != 0) return ret;
      goto out;
    }

    INIT_LIST_HEAD(&clist->list);
    if (ivec == 1 || clist->num_handlers == 0) {
      ret = -EINVAL;
      if (ivec != 1) return ret;
      goto out;
    }
    for (i = 0; i < clist->num_handlers; i++) {
      if (ivec == 2 ||
          (clist->handlers[i].request < IHK_OS_AUX_CALL_START ||
           clist->handlers[i].request > IHK_OS_AUX_CALL_END)) {
        ret = -EINVAL;
        if (ivec != 2) return ret;
        goto out;
      }
    }

    spin_lock_irqsave(&os->lock, flags);
    list_add_tail(&clist->list, &os->aux_call_list);
    spin_unlock_irqrestore(&os->lock, flags);

   out:
    BRANCH_RET_CHK(ret, b_infos[ivec].expected);

    spin_lock_irqsave(&os->lock, flags);
    list_for_each_entry(c_it, &os->aux_call_list, list) {
      count_list_after++;
    }
    spin_unlock_irqrestore(&os->lock, flags);

    if (ivec == total_branch - 1) {
      OKNG(count_list_after == count_list_prev + 1,
           "call list should be added 1 new entry\n");
    } else {
      OKNG(count_list_after == count_list_prev,
           "call list should not be changed\n");
    }
  }
  return 0;
 err:
  return -EINVAL;
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
EXPORT_SYMBOL(ihk_os_get_num_handlers);
EXPORT_SYMBOL(ihk_os_unregister_interrupt_handler);
EXPORT_SYMBOL(ihk_os_get_special_address);
EXPORT_SYMBOL(ihk_os_wait_for_status);
EXPORT_SYMBOL(ihk_host_find_dev);
EXPORT_SYMBOL(ihk_host_find_os);
EXPORT_SYMBOL(ihk_host_validate_os);
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
EXPORT_SYMBOL(ihk_os_get_ioctl_handlers);
EXPORT_SYMBOL(ihk_os_get_kernel_call_handlers);
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
EXPORT_SYMBOL(setup_monitor);
