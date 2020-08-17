/* driver.c COPYRIGHT FUJITSU LIMITED 2016 */
/**
 * \file executer/kernel/driver.c
 *  License details are found in the file LICENSE.
 * \brief
 *  kernel module entry
 * \author Taku Shimosawa  <shimosawa@is.s.u-tokyo.ac.jp> \par
 *      Copyright (C) 2011 - 2012  Taku Shimosawa
 * \author Balazs Gerofi  <bgerofi@riken.jp> \par
 *      Copyright (C) 2012  RIKEN AICS
 * \author Gou Nakamura  <go.nakamura.yw@hitachi-solutions.com> \par
 *      Copyright (C) 2012 - 2013 Hitachi, Ltd.
 * \author Tomoki Shirasawa  <tomoki.shirasawa.kk@hitachi-solutions.com> \par
 *      Copyright (C) 2012 - 2013 Hitachi, Ltd.
 * \author Balazs Gerofi  <bgerofi@is.s.u-tokyo.ac.jp> \par
 *      Copyright (C) 2013  The University of Tokyo
 */
/*
 * HISTORY:
 *  2013/09/02 shirasawa add terminate thread
 *  2013/08/19 shirasawa mcexec forward signal to MIC process
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/kallsyms.h>
#include <linux/version.h>

#include <driver/ihk_host_driver.h>
#include <driver/ihk_host_user.h>
#include "mcctrl.h"

#include "driver/okng_driver.h"
#include "driver/fs_utils.h"
#include "branch_info.h"

#define OS_MAX_MINOR 64

extern long __mcctrl_control(ihk_os_t, unsigned int, unsigned long,
                             struct file *);
extern int prepare_ikc_channels(ihk_os_t os);
extern void destroy_ikc_channels(ihk_os_t os);
#ifndef DO_USER_MODE
extern void mcctrl_syscall_init(void);
#endif
extern void procfs_init(int);
extern void procfs_exit(int);

extern void uti_attr_finalize(void);
extern void binfmt_mcexec_init(void);
extern void binfmt_mcexec_exit(void);

extern int mcctrl_os_read_cpu_register(ihk_os_t os, int cpu,
    struct ihk_os_cpu_register *desc);
extern int mcctrl_os_write_cpu_register(ihk_os_t os, int cpu,
    struct ihk_os_cpu_register *desc);
extern int mcctrl_get_request_os_cpu(ihk_os_t os, int *cpu);

static long mcctrl_ioctl(ihk_os_t os, unsigned int request, void *priv,
                         unsigned long arg, struct file *file)
{
  return __mcctrl_control(os, request, arg, file);
}

static struct ihk_os_user_call_handler mcctrl_uchs[] = {
  { .request = MCEXEC_UP_PREPARE_IMAGE, .func = mcctrl_ioctl },
  { .request = MCEXEC_UP_TRANSFER, .func = mcctrl_ioctl },
  { .request = MCEXEC_UP_START_IMAGE, .func = mcctrl_ioctl },
  { .request = MCEXEC_UP_WAIT_SYSCALL, .func = mcctrl_ioctl },
  { .request = MCEXEC_UP_RET_SYSCALL, .func = mcctrl_ioctl },
  { .request = MCEXEC_UP_LOAD_SYSCALL, .func = mcctrl_ioctl },
  { .request = MCEXEC_UP_SEND_SIGNAL, .func = mcctrl_ioctl },
  { .request = MCEXEC_UP_GET_CPU, .func = mcctrl_ioctl },
  { .request = MCEXEC_UP_GET_NODES, .func = mcctrl_ioctl },
  { .request = MCEXEC_UP_GET_CPUSET, .func = mcctrl_ioctl },
  { .request = MCEXEC_UP_CREATE_PPD, .func = mcctrl_ioctl },
  { .request = MCEXEC_UP_STRNCPY_FROM_USER, .func = mcctrl_ioctl },
  { .request = MCEXEC_UP_PREPARE_DMA, .func = mcctrl_ioctl },
  { .request = MCEXEC_UP_FREE_DMA, .func = mcctrl_ioctl },
  { .request = MCEXEC_UP_OPEN_EXEC, .func = mcctrl_ioctl },
  { .request = MCEXEC_UP_CLOSE_EXEC, .func = mcctrl_ioctl },
  { .request = MCEXEC_UP_GET_CRED, .func = mcctrl_ioctl },
  { .request = MCEXEC_UP_GET_CREDV, .func = mcctrl_ioctl },
#ifdef MCEXEC_BIND_MOUNT
  { .request = MCEXEC_UP_SYS_MOUNT, .func = mcctrl_ioctl },
  { .request = MCEXEC_UP_SYS_UMOUNT, .func = mcctrl_ioctl },
  { .request = MCEXEC_UP_SYS_UNSHARE, .func = mcctrl_ioctl },
#endif // MCEXEC_BIND_MOUNT
  { .request = MCEXEC_UP_UTI_GET_CTX, .func = mcctrl_ioctl },
  { .request = MCEXEC_UP_UTI_SWITCH_CTX, .func = mcctrl_ioctl },
  { .request = MCEXEC_UP_SIG_THREAD, .func = mcctrl_ioctl },
  { .request = MCEXEC_UP_SYSCALL_THREAD, .func = mcctrl_ioctl },
  { .request = MCEXEC_UP_TERMINATE_THREAD, .func = mcctrl_ioctl },
  { .request = MCEXEC_UP_GET_NUM_POOL_THREADS, .func = mcctrl_ioctl },
  { .request = MCEXEC_UP_UTI_ATTR, .func = mcctrl_ioctl },
  { .request = MCEXEC_UP_RELEASE_USER_SPACE, .func = mcctrl_ioctl },
  { .request = MCEXEC_UP_DEBUG_LOG, .func = mcctrl_ioctl },
  { .request = IHK_OS_AUX_PERF_NUM, .func = mcctrl_ioctl },
  { .request = IHK_OS_AUX_PERF_SET, .func = mcctrl_ioctl },
  { .request = IHK_OS_AUX_PERF_GET, .func = mcctrl_ioctl },
  { .request = IHK_OS_AUX_PERF_ENABLE, .func = mcctrl_ioctl },
  { .request = IHK_OS_AUX_PERF_DISABLE, .func = mcctrl_ioctl },
  { .request = IHK_OS_AUX_PERF_DESTROY, .func = mcctrl_ioctl },
  { .request = IHK_OS_GETRUSAGE, .func = mcctrl_ioctl },
};

static struct ihk_os_kernel_call_handler mcctrl_kernel_handlers = {
  .get_request_cpu = mcctrl_get_request_os_cpu,
  .read_cpu_register = mcctrl_os_read_cpu_register,
  .write_cpu_register = mcctrl_os_write_cpu_register,
};

static struct ihk_os_user_call mcctrl_uc_proto = {
  .num_handlers = sizeof(mcctrl_uchs) / sizeof(mcctrl_uchs[0]),
  .handlers = mcctrl_uchs,
};

static struct ihk_os_user_call mcctrl_uc[OS_MAX_MINOR];

static ihk_os_t os[OS_MAX_MINOR];

ihk_os_t osnum_to_os(int n)
{
  return os[n];
}

/* OS event notifier implementation */
int mcctrl_os_boot_notifier(int os_index)
{
  int  rc;

  os[os_index] = ihk_host_find_os(os_index, NULL);
  if (!os[os_index]) {
    printk("mcctrl: error: OS ID %d couldn't be found\n", os_index);
    return -EINVAL;
  }

  if (prepare_ikc_channels(os[os_index]) != 0) {
    printk("mcctrl: error: preparing IKC channels for OS %d\n", os_index);

    os[os_index] = NULL;
    return -EFAULT;
  }

  memcpy(mcctrl_uc + os_index, &mcctrl_uc_proto, sizeof mcctrl_uc_proto);

  rc = ihk_os_set_kernel_call_handlers(os[os_index], &mcctrl_kernel_handlers);
  if (rc < 0) {
    printk("mcctrl: error: setting kernel callbacks for OS %d\n", os_index);
    goto error_cleanup_channels;
  }

  rc = ihk_os_register_user_call_handlers(os[os_index], mcctrl_uc + os_index);
  if (rc < 0) {
    printk("mcctrl: error: registering callbacks for OS %d\n", os_index);
    goto error_clear_kernel_handlers;
  }

  procfs_init(os_index);
  printk("mcctrl: OS ID %d boot event handled\n", os_index);

  return 0;

error_clear_kernel_handlers:
  ihk_os_clear_kernel_call_handlers(os[os_index]);
error_cleanup_channels:
  destroy_ikc_channels(os[os_index]);

  os[os_index] = NULL;
  return rc;
}

int mcctrl_os_shutdown_notifier_orig(int os_index)
{
  if (os[os_index]) {
    pager_cleanup();
    sysfsm_cleanup(os[os_index]);
    free_topology_info(os[os_index]);
    ihk_os_unregister_user_call_handlers(os[os_index], mcctrl_uc + os_index);
    ihk_os_clear_kernel_call_handlers(os[os_index]);
    destroy_ikc_channels(os[os_index]);
    procfs_exit(os_index);
  }

  os[os_index] = NULL;

  printk("mcctrl: OS ID %d shutdown event handled\n", os_index);
  return 0;
}

int mcctrl_os_shutdown_notifier(int os_index)
{
  if (g_ihk_test_mode != TEST_MCCTRL_OS_SHUTDOWN_NOTIFIER)  // Disable test code
    return mcctrl_os_shutdown_notifier_orig(os_index);

  START("main case");

  unsigned long PATH_OS_EXIST = 1UL << 0;
  unsigned long exec_path = 0UL;

  if (os[os_index]) {
    pager_cleanup();
    sysfsm_cleanup(os[os_index]);
    free_topology_info(os[os_index]);
    ihk_os_unregister_user_call_handlers(os[os_index], mcctrl_uc + os_index);
    ihk_os_clear_kernel_call_handlers(os[os_index]);
    destroy_ikc_channels(os[os_index]);
    procfs_exit(os_index);

    exec_path |= PATH_OS_EXIST;
  }

  if (exec_path & PATH_OS_EXIST) {
    OKNG(!fs_os_procfs_entry_exist(os_index), "os procfs entries are removed\n");
    OKNG(!fs_os_sysfs_entry_exist(os_index), "os sysfs entries are removed\n");
    struct ihk_os_kernel_call_handler *_handlers =
      ihk_os_get_kernel_call_handlers(os[os_index]);
    OKNG(_handlers == NULL, "kernel call handlers should be cleared\n");

    struct list_head *aux_list = ihk_os_get_ioctl_handlers(os[os_index]);
    OKNG(list_empty(aux_list),
         "List of the additional ioctl handlers should be empty\n");

    // topology
    struct mcctrl_usrdata *udp = ihk_host_os_get_usrdata(os[os_index]);
    if (udp) {
      OKNG(list_empty(&udp->cpu_topology_list),
           "cpu topology list should be empty\n");
      OKNG(list_empty(&udp->node_topology_list),
           "node topology list should be empty\n");
    }
  }

  os[os_index] = NULL;

  OKNG(os[os_index] == NULL, "os cache list should be updated\n");

  printk("mcctrl: OS ID %d shutdown event handled\n", os_index);

  return 0;
 err:
  return -1;
}

ihk_test_mode_t g_ihk_test_mode;

void mcctrl_os_set_test_mode(int mode)
{
  g_ihk_test_mode = mode;
  printk("[IHK] mcctrl: set test mode to %d\n", mode);
}

int mcctrl_os_alive()
{
  int i;

  for (i = 0; i < OS_MAX_MINOR; i++)
    if (os[i])
      return i;
  return -1;
}

static struct ihk_os_notifier_ops mcctrl_os_notifier_ops = {
  .boot = mcctrl_os_boot_notifier,
  .shutdown = mcctrl_os_shutdown_notifier,
  .set_test_mode = mcctrl_os_set_test_mode,
};

static struct ihk_os_notifier mcctrl_os_notifier = {
  .ops = &mcctrl_os_notifier_ops,
};



int (*mcctrl_sys_mount)(char *dev_name, char *dir_name, char *type,
      unsigned long flags, void *data);
int (*mcctrl_sys_umount)(char *dir_name, int flags);
int (*mcctrl_sys_unshare)(unsigned long unshare_flags);
long (*mcctrl_sched_setaffinity)(pid_t pid, const struct cpumask *in_mask);
int (*mcctrl_sched_setscheduler_nocheck)(struct task_struct *p, int policy,
           const struct sched_param *param);

ssize_t (*mcctrl_sys_readlinkat)(int dfd, const char *path, char *buf,
             size_t bufsiz);
void (*mcctrl_zap_page_range)(struct vm_area_struct *vma,
            unsigned long start,
            unsigned long size,
            struct zap_details *details);

struct inode_operations *mcctrl_hugetlbfs_inode_operations;


static int symbols_init(void)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
  mcctrl_sys_mount = (void *) kallsyms_lookup_name("ksys_mount");
#else
  mcctrl_sys_mount = (void *) kallsyms_lookup_name("sys_mount");
#if defined(CONFIG_X86_64_SMP)
  if (!mcctrl_sys_mount)
    mcctrl_sys_mount =
      (void *) kallsyms_lookup_name("__x64_sys_mount");
#endif
#endif
  if (WARN_ON(!mcctrl_sys_mount))
    return -EFAULT;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
  mcctrl_sys_umount = (void *) kallsyms_lookup_name("ksys_umount");
#else
  mcctrl_sys_umount = (void *) kallsyms_lookup_name("sys_umount");
#if defined(CONFIG_X86_64_SMP)
  if (!mcctrl_sys_umount)
    mcctrl_sys_umount =
      (void *) kallsyms_lookup_name("__x64_sys_umount");
#endif
#endif
  if (WARN_ON(!mcctrl_sys_umount))
    return -EFAULT;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
  mcctrl_sys_unshare = (void *) kallsyms_lookup_name("ksys_unshare");
#else
  mcctrl_sys_unshare = (void *) kallsyms_lookup_name("sys_unshare");
#if defined(CONFIG_X86_64_SMP)
  if (!mcctrl_sys_unshare)
    mcctrl_sys_unshare =
      (void *) kallsyms_lookup_name("__x64_sys_unshare");
#endif
#endif
  if (WARN_ON(!mcctrl_sys_unshare))
    return -EFAULT;

  mcctrl_sched_setaffinity =
    (void *) kallsyms_lookup_name("sched_setaffinity");
  if (WARN_ON(!mcctrl_sched_setaffinity))
    return -EFAULT;

  mcctrl_sched_setscheduler_nocheck =
    (void *) kallsyms_lookup_name("sched_setscheduler_nocheck");
  if (WARN_ON(!mcctrl_sched_setscheduler_nocheck))
    return -EFAULT;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
  mcctrl_sys_readlinkat = (void *)kallsyms_lookup_name("do_readlinkat");
#else
  mcctrl_sys_readlinkat = (void *)kallsyms_lookup_name("sys_readlinkat");
#if defined(CONFIG_X86_64_SMP)
  if (!mcctrl_sys_readlinkat)
    mcctrl_sys_readlinkat =
      (void *) kallsyms_lookup_name("__x64_sys_readlinkat");
#endif
#endif
  if (WARN_ON(!mcctrl_sys_readlinkat))
    return -EFAULT;

  mcctrl_zap_page_range =
    (void *) kallsyms_lookup_name("zap_page_range");
  if (WARN_ON(!mcctrl_zap_page_range))
    return -EFAULT;

  mcctrl_hugetlbfs_inode_operations =
    (void *) kallsyms_lookup_name("hugetlbfs_inode_operations");
  if (WARN_ON(!mcctrl_hugetlbfs_inode_operations))
    return -EFAULT;

  return arch_symbols_init();
}

static int __init mcctrl_init(void)
{
  int ret = 0;
  int i;

#ifndef DO_USER_MODE
  mcctrl_syscall_init();
#endif

  for (i = 0; i < OS_MAX_MINOR; ++i) {
    os[i] = NULL;
  }

  binfmt_mcexec_init();

  if ((ret = symbols_init()))
    goto error;

  if ((ret = ihk_host_register_os_notifier(&mcctrl_os_notifier)) != 0) {
    printk("mcctrl: error: registering OS notifier\n");
    goto error;
  }

  printk("mcctrl: initialized successfully.\n");
  return ret;

error:
  binfmt_mcexec_exit();

  return ret;
}

static void __exit mcctrl_exit(void)
{
  if (ihk_host_deregister_os_notifier(&mcctrl_os_notifier) != 0) {
    printk("mcctrl: warning: failed to deregister OS notifier??\n");
  }

  binfmt_mcexec_exit();
  uti_attr_finalize();

  printk("mcctrl: unregistered.\n");
}

MODULE_LICENSE("GPL v2");
module_init(mcctrl_init);
module_exit(mcctrl_exit);
