/**
 * \file executer/kernel/ikc.c
 *  License details are found in the file LICENSE.
 * \brief
 *  inter kernel communication
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
 *  2013/08/07 nakamura add page fault forwarding
 *  2013/06/06 shirasawa propagate error code for prepare image
 *  2013/06/02 shirasawa add error handling for prepare_process
 */
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/interrupt.h>

#include <driver/ihk_host_user.h>
#include "mcctrl.h"
#ifdef ATTACHED_MIC
#include <sysdeps/mic/mic/micconst.h>
#endif

#include "driver/okng_driver.h"
#include "branch_info.h"

#define REQUEST_SHIFT    16

//#define DEBUG_IKC

#ifdef DEBUG_IKC
#define  dkprintf(...) kprintf(__VA_ARGS__)
#define  ekprintf(...) kprintf(__VA_ARGS__)
#else
#define dkprintf(...) do { if (0) printk(__VA_ARGS__); } while (0)
#define  ekprintf(...) printk(__VA_ARGS__)
#endif

//int num_channels;

//struct mcctrl_channel *channels;

static void mcctrl_ikc_init(ihk_os_t os, int cpu, unsigned long rphys, struct ihk_ikc_channel_desc *c);
int mcexec_syscall(struct mcctrl_usrdata *ud, struct ikc_scd_packet *packet);
void sig_done(unsigned long arg, int err);
void mcctrl_perf_ack(ihk_os_t os, struct ikc_scd_packet *packet);
void mcctrl_futex_wake(struct ikc_scd_packet *pisp);
void mcctrl_os_read_write_cpu_response(ihk_os_t os,
    struct ikc_scd_packet *pisp);
void mcctrl_eventfd(ihk_os_t os, struct ikc_scd_packet *pisp);

static void mcctrl_wakeup_desc_cleanup_orig(ihk_os_t os,
                                            struct mcctrl_wakeup_desc *desc)
{
  int i;

  list_del(&desc->chain);

  for (i = 0; i < desc->free_addrs_count; i++) {
    kfree(desc->free_addrs[i]);
  }
}

/* Assumes usrdata->wakeup_descs_lock taken */
static void mcctrl_wakeup_desc_cleanup(ihk_os_t os,
                                       struct mcctrl_wakeup_desc *desc)
{
  if (g_ihk_test_mode != TEST_MCCTRL_WAKEUP_DESC_CLEANUP)  // Disable test code
    return mcctrl_wakeup_desc_cleanup_orig(os, desc);

  unsigned long ivec = 0;
  unsigned long total_branch = 2;

  branch_info_t b_infos[] = {
    { 0, "invalid desc" },
    { 0, "main case" },
  };

  int count_desc_prev = 0;
  struct mcctrl_usrdata *usrdata = ihk_host_os_get_usrdata(os);
  struct mcctrl_wakeup_desc *mwd_entry;
  list_for_each_entry(mwd_entry, &usrdata->wakeup_descs_list, chain) {
    count_desc_prev++;
  }

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    int i;
    int count_desc_after = 0;
    if (ivec == 0 || !desc) {
      if (ivec == 0) goto out;
      return;
    }

    list_del(&desc->chain);

    for (i = 0; i < desc->free_addrs_count; i++) {
      kfree(desc->free_addrs[i]);
    }

   out:
    list_for_each_entry(mwd_entry, &usrdata->wakeup_descs_list, chain) {
      count_desc_after++;
    }

    if (ivec == total_branch - 1) {
      OKNG(count_desc_after == count_desc_prev - 1,
           "desc list should be decreased by 1\n");
    } else {
      OKNG(count_desc_after == count_desc_prev, "nothing to do\n");
    }
  }
 err:
  return;
}

static void mcctrl_wakeup_cb(ihk_os_t os, struct ikc_scd_packet *packet)
{
  struct mcctrl_wakeup_desc *desc = packet->reply;


  WRITE_ONCE(desc->err, packet->err);

  /*
   * Check if the other side is still waiting, and signal it we're done.
   *
   * Setting status needs to be done last because the other side could
   * wake up opportunistically between this set and the wake_up call.
   *
   * If the other side is no longer waiting, free the memory that was
   * left for us.
   */
  if (cmpxchg(&desc->status, 0, 1)) {
    struct mcctrl_usrdata *usrdata = ihk_host_os_get_usrdata(os);
    unsigned long flags;

    /* destroy_ikc_channels must have cleaned up descs */
    if (!usrdata) {
      pr_err("%s: error: mcctrl_usrdata not found\n", __func__);
      return;
    }

    spin_lock_irqsave(&usrdata->wakeup_descs_lock, flags);
    mcctrl_wakeup_desc_cleanup(os, desc);
    spin_unlock_irqrestore(&usrdata->wakeup_descs_lock, flags);
    return;
  }

  wake_up_interruptible(&desc->wq);
}

int mcctrl_ikc_send_wait_orig(
    ihk_os_t os, int cpu, struct ikc_scd_packet *pisp,
    long int timeout, struct mcctrl_wakeup_desc *desc,
    int *do_frees, int free_addrs_count, ...)
{
  int ret, i;
  int alloc_desc = (desc == NULL);
  va_list ap;

  if (free_addrs_count)
    *do_frees = 1;
  if (alloc_desc)
    desc = kmalloc(sizeof(struct mcctrl_wakeup_desc) +
             (free_addrs_count + 1) * sizeof(void *),
             GFP_KERNEL);
  if (!desc) {
    pr_warn("%s: Could not allocate wakeup descriptor", __func__);
    return -ENOMEM;
  }
  pisp->reply = desc;
  va_start(ap, free_addrs_count);
  for (i = 0; i < free_addrs_count; i++) {
    desc->free_addrs[i] = va_arg(ap, void*);
  }
  va_end(ap);
  if (alloc_desc)
    desc->free_addrs[free_addrs_count++] = desc;
  desc->free_addrs_count = free_addrs_count;
  init_waitqueue_head(&desc->wq);
  WRITE_ONCE(desc->err, 0);
  WRITE_ONCE(desc->status, 0);

  ret = mcctrl_ikc_send(os, cpu, pisp);
  if (ret < 0) {
    pr_warn("%s: mcctrl_ikc_send failed: %d\n", __func__, ret);
    kfree(desc);
    return ret;
  }

  if (timeout) {
    ret = wait_event_interruptible_timeout(desc->wq, desc->status, timeout);
  } else {
    ret = wait_event_interruptible(desc->wq, desc->status);
  }

  /*
   * Check if wait aborted (signal..) or timed out, and notify
   * the callback it will need to free things for us
   */
  if (!cmpxchg(&desc->status, 0, 1)) {
    struct mcctrl_usrdata *usrdata = ihk_host_os_get_usrdata(os);
    unsigned long flags;

    if (!usrdata) {
      pr_err("%s: error: mcctrl_usrdata not found\n", __func__);
      ret = ret < 0 ? ret : -EINVAL;
      goto out;
    }

    spin_lock_irqsave(&usrdata->wakeup_descs_lock, flags);
    list_add(&desc->chain, &usrdata->wakeup_descs_list);
    spin_unlock_irqrestore(&usrdata->wakeup_descs_lock, flags);
    if (do_frees)
      *do_frees = 0;
    return ret < 0 ? ret : -ETIME;
  }

  ret = READ_ONCE(desc->err);
 out:
  if (alloc_desc)
    kfree(desc);
  return ret;
}

/* do_frees: 1 when caller should free free_addrs[], 0 otherwise */
int mcctrl_ikc_send_wait(
    ihk_os_t os, int cpu, struct ikc_scd_packet *pisp,
    long int timeout, struct mcctrl_wakeup_desc *desc,
    int *do_frees, int free_addrs_count, ...)
{
  if (g_ihk_test_mode != TEST_MCCTRL_IKC_SEND_WAIT)  // Disable test code
    return mcctrl_ikc_send_wait_orig(os, cpu, pisp, timeout, desc, do_frees, free_addrs_count);

  unsigned long ivec = 0;
  unsigned long total_branch = 4;

  branch_info_t b_infos[] = {
    { -EINVAL, "mcctrl_ikc_send fail" },
    { -EINVAL, "usrdata not found" },
    { -ETIME,  "xchg desc status" },
    { 0,       "main case" },
  };

  int ret, i;
  int alloc_desc = (desc == NULL);
  int count_wakeup_list_prev = 0;
  int free_addrs_count_prev = free_addrs_count;
  struct mcctrl_usrdata *usrdata = NULL;
  struct mcctrl_wakeup_desc *it;
  unsigned long flags;

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    va_list ap;
    int count_wakeup_list_after = 0;
    int should_quit = 0;
    free_addrs_count = free_addrs_count_prev;

    if (free_addrs_count)
      *do_frees = 1;

    if (alloc_desc)
      desc = kmalloc(sizeof(struct mcctrl_wakeup_desc) +
               (free_addrs_count + 1) * sizeof(void *),
               GFP_KERNEL);

    if (!desc) {
      pr_warn("%s: Could not allocate wakeup descriptor", __func__);
      return -ENOMEM;
    }

    pisp->reply = desc;
    va_start(ap, free_addrs_count);
    for (i = 0; i < free_addrs_count; i++) {
      desc->free_addrs[i] = va_arg(ap, void*);
    }
    va_end(ap);
    if (alloc_desc)
      desc->free_addrs[free_addrs_count++] = desc;
    desc->free_addrs_count = free_addrs_count;

    init_waitqueue_head(&desc->wq);
    WRITE_ONCE(desc->err, 0);
    WRITE_ONCE(desc->status, 0);

    if (ivec > 0)
      ret = mcctrl_ikc_send(os, cpu, pisp);

    if (ivec == 0 || ret < 0) {
      ret = -EINVAL;
      if (ivec != 0) {
        pr_warn("%s: mcctrl_ikc_send failed: %d\n", __func__, ret);
        should_quit = 1;
      }
      goto out;
    }

    if (timeout) {
      ret = wait_event_interruptible_timeout(desc->wq, desc->status, timeout);
    } else {
      ret = wait_event_interruptible(desc->wq, desc->status);
    }

    /*
     * Check if wait aborted (signal..) or timed out, and notify
     * the callback it will need to free things for us
     */
    if (ivec <= 2 || !cmpxchg(&desc->status, 0, 1)) {
      usrdata = ihk_host_os_get_usrdata(os);
      if (ivec == 1 || !usrdata) {
        ret = -EINVAL;
        if (ivec != 1) {
          pr_err("%s: error: mcctrl_usrdata not found\n", __func__);
          should_quit = 1;
        }
        ret = ret < 0 ? ret : -EINVAL;
        goto out;
      }

      spin_lock_irqsave(&usrdata->wakeup_descs_lock, flags);
      list_for_each_entry(it, &usrdata->wakeup_descs_list, chain) {
        count_wakeup_list_prev++;
      }
      list_add(&desc->chain, &usrdata->wakeup_descs_list);
      spin_unlock_irqrestore(&usrdata->wakeup_descs_lock, flags);
      if (do_frees)
        *do_frees = 0;
      ret = -ETIME;
      goto out;
    }

    ret = READ_ONCE(desc->err);
   out:
    if (ivec == 2) {
      spin_lock_irqsave(&usrdata->wakeup_descs_lock, flags);
      list_for_each_entry(it, &usrdata->wakeup_descs_list, chain) {
        count_wakeup_list_after++;
      }
      list_del(&desc->chain);  // reset
      spin_unlock_irqrestore(&usrdata->wakeup_descs_lock, flags);
    }

    if (alloc_desc)
      kfree(desc);

    BRANCH_RET_CHK(ret, b_infos[ivec].expected);

    if (ivec == 2) {
      OKNG(count_wakeup_list_after == count_wakeup_list_prev + 1,
           "new desc should be added to wakeup list\n");
    }
  }
  return ret;
 err:
  return -EINVAL;
}

static int syscall_packet_handler_orig(struct ihk_ikc_channel_desc *c,
                                       void *__packet, void *__os)
{
  int ret = 0;
  struct ikc_scd_packet *pisp = __packet;
  struct mcctrl_usrdata *usrdata = ihk_host_os_get_usrdata(__os);
  int msg = pisp->msg;

  if (!usrdata) {
    pr_err("%s: error: mcctrl_usrdata not found\n", __func__);
    ret = -EINVAL;
    goto out;
  }

  switch (msg) {
  case SCD_MSG_INIT_CHANNEL:
    mcctrl_ikc_init(__os, pisp->ref, pisp->arg, c);
    break;

  case SCD_MSG_PREPARE_PROCESS_ACKED:
  case SCD_MSG_PERF_ACK:
  case SCD_MSG_SEND_SIGNAL_ACK:
  case SCD_MSG_PROCFS_ANSWER:
  case SCD_MSG_REMOTE_PAGE_FAULT_ANSWER:
  case SCD_MSG_CPU_RW_REG_RESP:
    mcctrl_wakeup_cb(__os, pisp);
    break;

  case SCD_MSG_SYSCALL_ONESIDE:
    mcexec_syscall(usrdata, pisp);
    break;

  case SCD_MSG_SYSFS_REQ_CREATE:
  case SCD_MSG_SYSFS_REQ_MKDIR:
  case SCD_MSG_SYSFS_REQ_SYMLINK:
  case SCD_MSG_SYSFS_REQ_LOOKUP:
  case SCD_MSG_SYSFS_REQ_UNLINK:
  case SCD_MSG_SYSFS_REQ_SETUP:
  case SCD_MSG_SYSFS_RESP_SHOW:
  case SCD_MSG_SYSFS_RESP_STORE:
  case SCD_MSG_SYSFS_RESP_RELEASE:
    sysfsm_packet_handler(__os, pisp->msg, pisp->err,
                          pisp->sysfs_arg1, pisp->sysfs_arg2);
    break;

  case SCD_MSG_PROCFS_TID_CREATE:
  case SCD_MSG_PROCFS_TID_DELETE:
    procfsm_packet_handler(__os, pisp->msg, pisp->pid, pisp->arg,
                           pisp->resp_pa);
    break;

  case SCD_MSG_GET_VDSO_INFO:
    get_vdso_info(__os, pisp->arg);
    break;

  case SCD_MSG_EVENTFD:
    dkprintf("%s: SCD_MSG_EVENTFD,pisp->eventfd_type=%d\n",
             __FUNCTION__, pisp->eventfd_type);
    mcctrl_eventfd(__os, pisp);
    break;

  case SCD_MSG_FUTEX_WAKE:
    mcctrl_futex_wake(pisp);
    break;

  default:
    printk(KERN_ERR "mcctrl:syscall_packet_handler:"
           "unknown message (%d.%d.%d.%d.%d.%#lx)\n",
           pisp->msg, pisp->ref, pisp->osnum, pisp->pid,
           pisp->err, pisp->arg);
    ret = -EINVAL;
    break;
  }

out:
  /*
   * SCD_MSG_SYSCALL_ONESIDE holds the packet and frees is it
   * mcexec_ret_syscall(), for the rest, free it here.
   */
  if (!ret && msg != SCD_MSG_SYSCALL_ONESIDE) {
    ihk_ikc_release_packet((struct ihk_ikc_free_packet *)__packet);
  }
  return ret;
}

/* XXX: this runs in atomic context! */
static int syscall_packet_handler(struct ihk_ikc_channel_desc *c,
                                  void *__packet, void *__os)
{
  if (g_ihk_test_mode != TEST_SYSCALL_PACKET_HANDLER)  // Disable test code
    return syscall_packet_handler_orig(c, __packet, __os);

  unsigned long ivec = 0;
  unsigned long total_branch = 4;

  branch_info_t b_infos[] = {
    { -EINVAL, "invalid parameter" },
    { -EINVAL, "usrdata not found" },
    { -EINVAL, "invalid msg" },
    { 0,       "main case" },
  };

  int msg_prev = 0;

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    int ret = 0;
    struct ikc_scd_packet *pisp = __packet;
    struct mcctrl_usrdata *usrdata = NULL;

    if (ivec == 0 || (!c || !pisp || !__os || ihk_host_validate_os(__os))) {
      ret = -EINVAL;
      if (ivec != 0) return ret;
      goto out;
    }

    if (ivec <= 2)
      msg_prev = pisp->msg;
    else
      pisp->msg = msg_prev;
    usrdata = ihk_host_os_get_usrdata(__os);
    if (ivec == 1 || !usrdata) {
      ret = -EINVAL;
      if (ivec != 1) {
        pr_err("%s: error: mcctrl_usrdata not found\n", __func__);
        return ret;
      }
      goto out;
    }

    if (ivec == 2) pisp->msg = 0;  // fake invalid msg
    ret = syscall_packet_handler_orig(c, __packet, __os);

   out:
    BRANCH_RET_CHK(ret, b_infos[ivec].expected);
  }

  return 0;
 err:
  return -EINVAL;
}

static int dummy_packet_handler(struct ihk_ikc_channel_desc *c,
                                  void *__packet, void *__os)
{
  kprintf("%s: WARNING: packet received\n", __FUNCTION__);
  ihk_ikc_release_packet((struct ihk_ikc_free_packet *)__packet);
  return 0;
}

int mcctrl_ikc_send_orig(ihk_os_t os, int cpu, struct ikc_scd_packet *pisp)
{
  struct mcctrl_usrdata *usrdata;

  if (!os || cpu < 0) {
    return -EINVAL;
  }

  usrdata = ihk_host_os_get_usrdata(os);
  if (!usrdata || cpu >= usrdata->num_channels ||
      !usrdata->channels[cpu].c) {
    return -EINVAL;
  }
  return ihk_ikc_send(usrdata->channels[cpu].c, pisp, 0);
}

int mcctrl_ikc_send(ihk_os_t os, int cpu, struct ikc_scd_packet *pisp)
{
  if (g_ihk_test_mode != TEST_MCCTRL_IKC_SEND)  // Disable test code
    return mcctrl_ikc_send_orig(os, cpu, pisp);

  unsigned long ivec = 0;
  unsigned long total_branch = 4;

  branch_info_t b_infos[] = {
    { -EINVAL, "invalid os instance or invalid packet" },
    { -EINVAL, "invalid cpu" },
    { -EINVAL, "invalid usrdata or invalid channel" },
    { 0,       "main case" },
  };

  int ret = 0;

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    struct mcctrl_usrdata *usrdata;

    if (ivec == 0 || (!os || ihk_host_validate_os(os) || !pisp)) {
      ret = -EINVAL;
      if (ivec != 0) return ret;
      goto out;
    }

    if (ivec == 1 || cpu < 0 || cpu >= 512) {
      ret = -EINVAL;
      if (ivec != 1) return ret;
      goto out;
    }

    usrdata = ihk_host_os_get_usrdata(os);
    if (ivec == 2 ||
        (!usrdata || cpu >= usrdata->num_channels ||
         !usrdata->channels[cpu].c)) {
      ret = -EINVAL;
      if (ivec != 2) return ret;
      goto out;
    }

    ret = ihk_ikc_send(usrdata->channels[cpu].c, pisp, 0);

   out:
    BRANCH_RET_CHK(ret, b_infos[ivec].expected);
  }
 err:
  return ret;
}

int mcctrl_ikc_send_msg(ihk_os_t os, int cpu, int msg, int ref, unsigned long arg)
{
  struct ikc_scd_packet packet;
  struct mcctrl_usrdata *usrdata;

  if (!os) {
    return -EINVAL;
  }

  usrdata = ihk_host_os_get_usrdata(os);
  if (!usrdata || cpu < 0 || cpu >= usrdata->num_channels ||
      !usrdata->channels[cpu].c) {
    return -EINVAL;
  }

  packet.msg = msg;
  packet.ref = ref;
  packet.arg = arg;

  return ihk_ikc_send(usrdata->channels[cpu].c, &packet, 0);
}

int mcctrl_ikc_set_recv_cpu(ihk_os_t os, int cpu)
{
  struct mcctrl_usrdata *usrdata;

  if (!os) {
    return -EINVAL;
  }

  usrdata = ihk_host_os_get_usrdata(os);
  if (!usrdata) {
    pr_err("%s: error: mcctrl_usrdata not found\n", __func__);
    return -EINVAL;
  }

  ihk_ikc_channel_set_cpu(usrdata->channels[cpu].c,
                          ihk_ikc_get_processor_id());
  return 0;
}

int mcctrl_ikc_is_valid_thread(ihk_os_t os, int cpu)
{
  struct mcctrl_usrdata *usrdata;

  if (!os) {
    return 0;
  }

  usrdata = ihk_host_os_get_usrdata(os);
  if (!usrdata || cpu < 0 || cpu >= usrdata->num_channels ||
      !usrdata->channels[cpu].c) {
    return 0;
  } else {
    return 1;
  }
}

static void mcctrl_ikc_init(ihk_os_t os, int cpu, unsigned long rphys, struct ihk_ikc_channel_desc *c)
{
  struct mcctrl_usrdata *usrdata;
  struct ikc_scd_packet packet;
  struct mcctrl_channel *pmc;

  if (!os) {
    pr_err("%s: error: os not found\n", __func__);
    return;
  }

  usrdata = ihk_host_os_get_usrdata(os);
  if (!usrdata) {
    pr_err("%s: error: mcctrl_usrdata not found\n", __func__);
    return;
  }

  if (c->port == 502) {
    pmc = usrdata->channels + usrdata->num_channels - 1;
  } else {
    pmc = usrdata->channels + cpu;
  }

  if (!pmc) {
    kprintf("%s: error: no channel found?\n", __FUNCTION__);
    return;
  }

  packet.msg = SCD_MSG_INIT_CHANNEL_ACKED;
  packet.ref = cpu;
  packet.arg = rphys;

  ihk_ikc_send(pmc->c, &packet, 0);
}

static int connect_handler_ikc2linux(struct ihk_ikc_channel_info *param)
{
  struct ihk_ikc_channel_desc *c;
  ihk_os_t os = param->channel->remote_os;
  struct mcctrl_usrdata  *usrdata = ihk_host_os_get_usrdata(os);
  int linux_cpu;

  if (!usrdata) {
    pr_err("%s: error: mcctrl_usrdata not found\n", __func__);
    return -1;
  }

  c = param->channel;
  linux_cpu = c->send.queue->write_cpu;
  if (linux_cpu > nr_cpu_ids) {
    kprintf("%s: invalid Linux CPU id %d\n",
        __FUNCTION__, linux_cpu);
    return -1;
  }
  dkprintf("%s: Linux CPU: %d\n", __FUNCTION__, linux_cpu);

  param->packet_handler = syscall_packet_handler;
  usrdata->ikc2linux[linux_cpu] = c;

  return 0;
}

static int connect_handler_ikc2mckernel(struct ihk_ikc_channel_info *param)
{
  struct ihk_ikc_channel_desc *c;
  int mck_cpu;
  ihk_os_t os = param->channel->remote_os;
  struct mcctrl_usrdata  *usrdata = ihk_host_os_get_usrdata(os);

  if (!usrdata) {
    pr_err("%s: error: mcctrl_usrdata not found\n", __func__);
    return 1;
  }

  c = param->channel;
  mck_cpu = c->send.queue->read_cpu;

  if (mck_cpu < 0 || mck_cpu >= usrdata->num_channels) {
    kprintf("Invalid connect source processor: %d\n", mck_cpu);
    return 1;
  }
  param->packet_handler = dummy_packet_handler;

  usrdata->channels[mck_cpu].c = c;

  return 0;
}

static struct ihk_ikc_listen_param lp_ikc2linux = {
  .port = 503,
  .ikc_direction = IHK_IKC_DIRECTION_RECV,
  .handler = connect_handler_ikc2linux,
  .pkt_size = sizeof(struct ikc_scd_packet),
  .queue_size = PAGE_SIZE * 4,
  .magic = 0x1129,
};

static struct ihk_ikc_listen_param lp_ikc2mckernel = {
  .port = 501,
  .ikc_direction = IHK_IKC_DIRECTION_SEND,
  .handler = connect_handler_ikc2mckernel,
  .pkt_size = sizeof(struct ikc_scd_packet),
  .queue_size = PAGE_SIZE * 4,
  .magic = 0x1329,
};

int prepare_ikc_channels_orig(ihk_os_t os)
{
  struct mcctrl_usrdata *usrdata;
  int i;
  int ret = 0;

  usrdata = kzalloc(sizeof(struct mcctrl_usrdata), GFP_ATOMIC);
  if (!usrdata) {
    printk("%s: error: allocating mcctrl_usrdata\n", __FUNCTION__);
    ret = -ENOMEM;
    goto error;
  }

  usrdata->cpu_info = ihk_os_get_cpu_info(os);
  usrdata->mem_info = ihk_os_get_memory_info(os);

  if (!usrdata->cpu_info || !usrdata->mem_info) {
    printk("%s: cannot obtain OS CPU and memory information.\n",
           __FUNCTION__);
    ret = -EINVAL;
    goto error;
  }

  if (usrdata->cpu_info->n_cpus < 1) {
    printk("%s: Error: # of cpu is invalid.\n", __FUNCTION__);
    ret = -EINVAL;
    goto error;
  }

  usrdata->num_channels = usrdata->cpu_info->n_cpus;
  usrdata->channels = kzalloc(sizeof(struct mcctrl_channel) *
                              usrdata->num_channels,
                              GFP_ATOMIC);

  if (!usrdata->channels) {
    printk("Error: cannot allocate channels.\n");
    ret = -ENOMEM;
    goto error;
  }

  usrdata->ikc2linux = kzalloc(sizeof(struct ihk_ikc_channel_desc *) *
                               nr_cpu_ids, GFP_ATOMIC);

  if (!usrdata->ikc2linux) {
    printk("Error: cannot allocate ikc2linux channels.\n");
    ret = -ENOMEM;
    goto error;
  }

  usrdata->os = os;
  ihk_host_os_set_usrdata(os, usrdata);

  ihk_ikc_listen_port(os, &lp_ikc2linux);
  ihk_ikc_listen_port(os, &lp_ikc2mckernel);

  init_waitqueue_head(&usrdata->wq_procfs);
  mutex_init(&usrdata->reserve_lock);

  for (i = 0; i < MCCTRL_PER_PROC_DATA_HASH_SIZE; ++i) {
    INIT_LIST_HEAD(&usrdata->per_proc_data_hash[i]);
    rwlock_init(&usrdata->per_proc_data_hash_lock[i]);
  }

  INIT_LIST_HEAD(&usrdata->cpu_topology_list);
  INIT_LIST_HEAD(&usrdata->node_topology_list);

  mutex_init(&usrdata->part_exec.lock);
  INIT_LIST_HEAD(&usrdata->part_exec.pli_list);
  usrdata->part_exec.nr_processes = -1;
  INIT_LIST_HEAD(&usrdata->wakeup_descs_list);
  spin_lock_init(&usrdata->wakeup_descs_lock);

  return 0;

 error:
  if (usrdata) {
    if (usrdata->channels) kfree(usrdata->channels);
    if (usrdata->ikc2linux) kfree(usrdata->ikc2linux);
    kfree(usrdata);
  }

  return ret;
}

int prepare_ikc_channels(ihk_os_t os)
{
  if (g_ihk_test_mode != TEST_PREPARE_IKC_CHANNELS)  // Disable test code
    return prepare_ikc_channels_orig(os);

  unsigned long ivec = 0;
  unsigned long total_branch = 3;

  branch_info_t b_infos[] = {
    { -EINVAL, "cannot get os cpu or mem info" },
    { -EINVAL, "invalid # of cpus" },
    { 0,       "main case" },
  };

  int i;

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    struct mcctrl_usrdata *usrdata;
    int ret = 0;
    int should_quit = 0;

    usrdata = kzalloc(sizeof(struct mcctrl_usrdata), GFP_ATOMIC);
    if (!usrdata) {
      printk("%s: error: allocating mcctrl_usrdata\n", __FUNCTION__);
      ret = -ENOMEM;
      should_quit = 1;
      goto out;
    }

    usrdata->cpu_info = ihk_os_get_cpu_info(os);
    usrdata->mem_info = ihk_os_get_memory_info(os);

    if (ivec == 0 || (!usrdata->cpu_info || !usrdata->mem_info)) {
      if (ivec != 0) {
        printk("%s: cannot obtain OS CPU and memory information.\n",
               __FUNCTION__);
        should_quit = 1;
      }
      ret = -EINVAL;
      goto out;
    }

    if (ivec == 1 || usrdata->cpu_info->n_cpus < 1) {
      if (ivec != 1) {
        printk("%s: Error: # of cpu is invalid.\n", __FUNCTION__);
        should_quit = 1;
      }
      ret = -EINVAL;
      goto out;
    }

    usrdata->num_channels = usrdata->cpu_info->n_cpus;
    usrdata->channels = kzalloc(sizeof(struct mcctrl_channel) *
                                usrdata->num_channels,
                                GFP_ATOMIC);

    if (!usrdata->channels) {
      printk("Error: cannot allocate channels.\n");
      ret = -ENOMEM;
      should_quit = 1;
      goto out;
    }

    usrdata->ikc2linux = kzalloc(sizeof(struct ihk_ikc_channel_desc *) *
                                 nr_cpu_ids, GFP_ATOMIC);

    if (!usrdata->ikc2linux) {
      printk("Error: cannot allocate ikc2linux channels.\n");
      ret = -ENOMEM;
      should_quit = 1;
      goto out;
    }

    usrdata->os = os;
    ihk_host_os_set_usrdata(os, usrdata);

    ihk_ikc_listen_port(os, &lp_ikc2linux);
    ihk_ikc_listen_port(os, &lp_ikc2mckernel);

    init_waitqueue_head(&usrdata->wq_procfs);
    mutex_init(&usrdata->reserve_lock);

    for (i = 0; i < MCCTRL_PER_PROC_DATA_HASH_SIZE; ++i) {
      INIT_LIST_HEAD(&usrdata->per_proc_data_hash[i]);
      rwlock_init(&usrdata->per_proc_data_hash_lock[i]);
    }

    INIT_LIST_HEAD(&usrdata->cpu_topology_list);
    INIT_LIST_HEAD(&usrdata->node_topology_list);

    mutex_init(&usrdata->part_exec.lock);
    INIT_LIST_HEAD(&usrdata->part_exec.pli_list);
    usrdata->part_exec.nr_processes = -1;
    INIT_LIST_HEAD(&usrdata->wakeup_descs_list);
    spin_lock_init(&usrdata->wakeup_descs_lock);

    ret = 0;

   out:
    if (ret && usrdata) {
      if (usrdata->channels) kfree(usrdata->channels);
      if (usrdata->ikc2linux) kfree(usrdata->ikc2linux);
      kfree(usrdata);
    }
    if (should_quit) return ret;

    BRANCH_RET_CHK(ret, b_infos[ivec].expected);

    if (ivec == total_branch - 1) {
      OKNG(usrdata->cpu_info && usrdata->mem_info &&
           usrdata->num_channels && usrdata->channels &&
           usrdata->ikc2linux, "all data is prepared\n");
      OKNG(ihk_host_os_get_usrdata(os), "usrdata is set\n");
    } else {
      OKNG(!ihk_host_os_get_usrdata(os), "usrdata is not set\n");
    }
  }
  return 0;
 err:
  return -EINVAL;
}

void __destroy_ikc_channel(ihk_os_t os, struct mcctrl_channel *pmc)
{
  return;
}

void destroy_ikc_channels_orig(ihk_os_t os)
{
  int i;
  unsigned long flags;
  struct mcctrl_usrdata *usrdata = ihk_host_os_get_usrdata(os);
  struct mcctrl_wakeup_desc *mwd_entry, *mwd_next;

  if (!usrdata) {
    kprintf("%s: error: mcctrl_usrdata not found\n", __func__);
    return;
  }

  ihk_host_os_set_usrdata(os, NULL);

  for (i = 0; i < usrdata->num_channels; i++) {
    if (usrdata->channels[i].c) {
      ihk_ikc_destroy_channel(usrdata->channels[i].c);
    }
  }

  for (i = 0; i < nr_cpu_ids; i++) {
    if (usrdata->ikc2linux[i]) {
      ihk_ikc_destroy_channel(usrdata->ikc2linux[i]);
    }
  }
  spin_lock_irqsave(&usrdata->wakeup_descs_lock, flags);
  list_for_each_entry_safe(mwd_entry, mwd_next,
                           &usrdata->wakeup_descs_list, chain) {
    mcctrl_wakeup_desc_cleanup(os, mwd_entry);
  }
  spin_unlock_irqrestore(&usrdata->wakeup_descs_lock, flags);

  kfree(usrdata->channels);
  kfree(usrdata->ikc2linux);
  kfree(usrdata);
}

void destroy_ikc_channels(ihk_os_t os)
{
  if (g_ihk_test_mode != TEST_DESTROY_IKC_CHANNELS)  // Disable test code
    return destroy_ikc_channels_orig(os);

  unsigned long ivec = 0;
  unsigned long total_branch = 6;

  branch_info_t b_infos[] = {
    { 0, "usrdata not found" },
    { 0, "no any channels exist" },
    { 0, "all channels are null" },
    { 0, "all ikc2linux channels are null" },
    { 0, "wakeup_descs_list is empty" },
    { 0, "main case" },
  };

  struct mcctrl_usrdata *usrdata_prev = ihk_host_os_get_usrdata(os);

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    int i;
    unsigned long flags;
    struct mcctrl_usrdata *usrdata = ihk_host_os_get_usrdata(os);
    struct mcctrl_wakeup_desc *mwd_entry, *mwd_next;

    if (ivec == 0 || !usrdata) {
      if (ivec != 0) {
        kprintf("%s: error: mcctrl_usrdata not found\n", __func__);
        return;
      }
      goto out;
    }

    ihk_host_os_set_usrdata(os, NULL);

    if (ivec >= 1 && ivec <= 3) goto skip1;
    for (i = 0; i < usrdata->num_channels; i++) {
      if (usrdata->channels[i].c) {
        ihk_ikc_destroy_channel(usrdata->channels[i].c);
      }
    }
   skip1:
    if (ivec >= 1 && ivec <= 3) goto skip2;

    for (i = 0; i < nr_cpu_ids; i++) {
      if (ivec == 3 || !usrdata->ikc2linux[i]) continue;
      if (usrdata->ikc2linux[i]) {
        ihk_ikc_destroy_channel(usrdata->ikc2linux[i]);
      }
    }
   skip2:
    if (ivec >= 1 && ivec <= 3) goto out;

    spin_lock_irqsave(&usrdata->wakeup_descs_lock, flags);
    if (ivec == 4 || list_empty(&usrdata->wakeup_descs_list)) {
      if (ivec == 4) {
        spin_unlock_irqrestore(&usrdata->wakeup_descs_lock, flags);
        goto out;
      }
    }
    list_for_each_entry_safe(mwd_entry, mwd_next,
                             &usrdata->wakeup_descs_list, chain) {
      mcctrl_wakeup_desc_cleanup(os, mwd_entry);
    }
    spin_unlock_irqrestore(&usrdata->wakeup_descs_lock, flags);

    kfree(usrdata->channels);
    kfree(usrdata->ikc2linux);
    kfree(usrdata);

   out:
    if (ivec == 0) {
      OKNG(1, "nothing to do\n");
    }
    if (ivec >= 1) {
      OKNG(ihk_host_os_get_usrdata(os) == NULL, "usrdata is released\n");
    }
    if (ivec == total_branch - 1) {
      OKNG(list_empty(&usrdata->wakeup_descs_list),
           "wakeup descs list should be empty\n");
    }
    // reset for next loop
    ihk_host_os_set_usrdata(os, usrdata_prev);
  }

 err:
  return;
}

void
mcctrl_eventfd(ihk_os_t os, struct ikc_scd_packet *pisp)
{
  ihk_os_eventfd(os, pisp->eventfd_type);
}
