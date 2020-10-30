/* mikc.c COPYRIGHT FUJITSU LIMITED 2015-2016 */
/**
 * \file host/linux/mikc.c
 *
 * \brief IHK-Host: Establishes a master channel, and also defines functions
 *        used in IHK-IKC.
 *
 * \author Taku Shimosawa  <shimosawa@is.s.u-tokyo.ac.jp> \par
 * Copyright (C) 2011 - 2012  Taku Shimosawa
 *
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
#include <asm/spinlock.h>

#include <driver/ihk_host_driver.h>
#include <driver/ihk_host_user.h>
#include <ihk/misc/debug.h>
#include <ikc/queue.h>
#include <ikc/msg.h>
#include "host_linux.h"

#include "driver/okng_driver.h"
#include "branch_info.h"
#include "ops_wrappers.h"
//#define DEBUG_IKC

#ifdef DEBUG_IKC
#define	dkprintf(...) kprintf(__VA_ARGS__)
#define	ekprintf(...) kprintf(__VA_ARGS__)
#else
#define dkprintf(...) do { if (0) printk(__VA_ARGS__); } while (0)
#define	ekprintf(...) printk(__VA_ARGS__)
#endif

static int arch_master_handler(struct ihk_ikc_channel_desc *c,
                               void *__packet, void *__os);

struct ihk_ikc_channel_desc *ihk_host_ikc_init_first_orig(ihk_os_t ihk_os,
                                                          ihk_ikc_ph_t handler)
{
	struct ihk_host_linux_os_data *os = ihk_os;
	unsigned long r, w, rp, wp, rsz, wsz;
	struct ihk_ikc_queue_head *rq, *wq;
	struct ihk_ikc_channel_desc *c;

	ihk_ikc_system_init(ihk_os);
	os->ikc_initialized = 1;

	if (ihk_os_wait_for_status(ihk_os, IHK_OS_STATUS_READY, 0, 200) == 0) {
		/* XXX:
		 * We assume this address is remote,
		 * but the local is possible... */
		dprintf("OS is now marked ready.\n");

		ihk_os_get_special_address(ihk_os, IHK_SPADDR_MIKC_QUEUE_RECV,
		                           &r, &rsz);
		ihk_os_get_special_address(ihk_os, IHK_SPADDR_MIKC_QUEUE_SEND,
		                           &w, &wsz);

		dprintf("MIKC rq: 0x%lX, wq: 0x%lX\n", r, w);

		rp = ihk_device_map_memory(os->dev_data, r, rsz);
		wp = ihk_device_map_memory(os->dev_data, w, wsz);

		rq = ihk_device_map_virtual(os->dev_data, rp, rsz, NULL, 0);
		wq = ihk_device_map_virtual(os->dev_data, wp, wsz, NULL, 0);

		c = kzalloc(sizeof(struct ihk_ikc_channel_desc)
		            + sizeof(struct ihk_ikc_master_packet), GFP_KERNEL);
		ihk_ikc_init_desc(c, ihk_os, 0, rq, wq,
		                  ihk_ikc_master_channel_packet_handler, c);

		ihk_ikc_channel_set_cpu(c, 0);

		c->recv.qphys = rp;
		c->send.qphys = wp;
		c->recv.qrphys = r;
		c->send.qrphys = w;
		/*
		 * ihk_ikc_interrupt_handler() on the LWK now iterates the channel
		 * until all packets are purged. This makes the notification IRQ
		 * on master channel unnecessary.
		 */
		//c->flag |= IKC_FLAG_NO_COPY;

		dprintf("c->remote_os = %p\n", c->remote_os);
		os->packet_handler = handler;

		return c;
	} else {
		printk("IHK: OS does not become ready, kernel msg:\n");
		ihk_host_print_os_kmsg(ihk_os);
		return NULL;
	}
}

/**
 * \brief Core function of initialization of a master channel.
 * It waits for the kernel to become ready, maps the queues,
 * and allocates a channel descriptor strcuture for the master channel.
 */
struct ihk_ikc_channel_desc *ihk_host_ikc_init_first(ihk_os_t ihk_os,
                                                     ihk_ikc_ph_t handler)
{
  if (g_ihk_test_mode != TEST_IHK_HOST_IKC_INIT_FIRST)  // Disable test code
    return ihk_host_ikc_init_first_orig(ihk_os, handler);

  unsigned long ivec = 0;
  unsigned long total_branch = 2;

  branch_info_t b_infos[] = {
    { 0, "OS Ready status is not reached" },
    { 0, "main case" },
  };

  struct ihk_host_linux_os_data *os = ihk_os;
  struct ihk_ikc_channel_desc *ret = NULL;

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

  	unsigned long r, w, rp, wp, rsz, wsz;
  	struct ihk_ikc_queue_head *rq, *wq;
  	struct ihk_ikc_channel_desc *c;

    if (ivec > 0)
  	  ihk_ikc_system_init(ihk_os);
  	os->ikc_initialized = 1;

  	if (ivec != 0 &&
        ihk_os_wait_for_status(ihk_os, IHK_OS_STATUS_READY, 0, 200) == 0) {
  		/* XXX:
  		 * We assume this address is remote,
  		 * but the local is possible... */
  		dprintf("OS is now marked ready.\n");

  		ihk_os_get_special_address(ihk_os, IHK_SPADDR_MIKC_QUEUE_RECV,
  		                           &r, &rsz);
  		ihk_os_get_special_address(ihk_os, IHK_SPADDR_MIKC_QUEUE_SEND,
  		                           &w, &wsz);

  		dprintf("MIKC rq: 0x%lX, wq: 0x%lX\n", r, w);

  		rp = ihk_device_map_memory(os->dev_data, r, rsz);
  		wp = ihk_device_map_memory(os->dev_data, w, wsz);

  		rq = ihk_device_map_virtual(os->dev_data, rp, rsz, NULL, 0);
  		wq = ihk_device_map_virtual(os->dev_data, wp, wsz, NULL, 0);

  		c = kzalloc(sizeof(struct ihk_ikc_channel_desc)
  		            + sizeof(struct ihk_ikc_master_packet), GFP_KERNEL);
  		ihk_ikc_init_desc(c, ihk_os, 0, rq, wq,
  		                  ihk_ikc_master_channel_packet_handler, c);

  		ihk_ikc_channel_set_cpu(c, 0);

  		c->recv.qphys = rp;
  		c->send.qphys = wp;
  		c->recv.qrphys = r;
  		c->send.qrphys = w;
  		/*
  		 * ihk_ikc_interrupt_handler() on the LWK now iterates the channel
  		 * until all packets are purged. This makes the notification IRQ
  		 * on master channel unnecessary.
  		 */
  		//c->flag |= IKC_FLAG_NO_COPY;

  		dprintf("c->remote_os = %p\n", c->remote_os);
  		os->packet_handler = handler;

  		ret = c;
  	} else {  // ivec = 0 goes here
      if (ivec != 0) {
  		  printk("IHK: OS does not become ready, kernel msg:\n");
  		  ihk_host_print_os_kmsg(ihk_os);
      }
  		ret = NULL;
      goto out;
  	}

   out:
    if (ivec == total_branch - 1) {
      OKNG(__ihk_os_query_status(os) == IHK_OS_STATUS_READY,
           "os status should be ready\n");
      OKNG(ret, "channel desc should be created\n");
    } else {
      OKNG(__ihk_os_query_status(os) != IHK_OS_STATUS_READY,
           "os status is not ready\n");
      OKNG(!ret, "channel desc is not created\n");
    }
  }
  return ret;
 err:
  return NULL;
}

int ihk_ikc_master_init_orig(ihk_os_t __os)
{
	struct ihk_host_linux_os_data *os = __os;
	struct ihk_ikc_master_packet packet;

	dprintf("ikc_master_init\n");

	if (!os) {
		return -EINVAL;
	}

	os->mchannel = ihk_host_ikc_init_first(os, arch_master_handler);
	dprintf("os(%p)->mchannel = %p\n", os, os->mchannel);
	if (!os->mchannel) {
		return -EINVAL;
	} else {
		ihk_ikc_enable_channel(os->mchannel);

		dprintf("ikc_master_init done.\n");

		/* ack send */
		packet.msg = IHK_IKC_MASTER_MSG_INIT_ACK;
		ihk_ikc_send(os->mchannel, &packet, 0);

		return 0;
	}
}

/** \brief Initializes a master channel */
int ihk_ikc_master_init(ihk_os_t __os)
{
  if (g_ihk_test_mode != TEST_IHK_IKC_MASTER_INIT)  // Disable test code
    return ihk_ikc_master_init_orig(__os);

  unsigned long ivec = 0;
  unsigned long total_branch = 3;

  branch_info_t b_infos[] = {
    { -EINVAL, "invalid os data" },
    { -EINVAL, "cannot create master channel" },
    { 0,       "main case" },
  };

  dprintf("ikc_master_init\n");
  struct ihk_host_linux_os_data *os = __os;
  int ret;

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

  	struct ihk_ikc_master_packet packet;

  	if (ivec == 0 || !os) {
  		ret = -EINVAL;
      if (ivec != 0) return ret;
      goto out;
  	}

    if (ivec > 1) {
  	  os->mchannel = ihk_host_ikc_init_first(os, arch_master_handler);
  	  dprintf("os(%p)->mchannel = %p\n", os, os->mchannel);
    }
  	if (ivec == 1 || !os->mchannel) {
  		ret = -EINVAL;
      if (ivec != 1) return ret;
      goto out;
  	} else {
  		ihk_ikc_enable_channel(os->mchannel);

  		dprintf("ikc_master_init done.\n");

  		/* ack send */
  		packet.msg = IHK_IKC_MASTER_MSG_INIT_ACK;
  		ihk_ikc_send(os->mchannel, &packet, 0);

  		ret = 0;
  	}
   out:
    BRANCH_RET_CHK(ret, b_infos[ivec].expected);

    if (ivec == total_branch - 1) {
      OKNG(os->mchannel, "master channel should be created\n");
      OKNG(os->mchannel->flag & IKC_FLAG_ENABLED,
           "master channel should be enabled\n");
    } else {
      OKNG(!os->mchannel, "master channel is not created\n");
    }
  }
  return ret;
 err:
  return -EINVAL;
}

void ikc_master_finalize_orig(ihk_os_t __os)
{
	struct ihk_host_linux_os_data *os = __os;

	dprint_func_enter;

	if (!os->ikc_initialized) {
		return;
	}

	if (os->mchannel) {
		ihk_ikc_destroy_channel(os->mchannel);
	}
	ihk_ikc_system_exit(os);

	os->ikc_initialized = 0;
}

extern int ihk_os_get_num_handlers(ihk_os_t os);

/** \brief Called when the kernel is going to shutdown. It finalizes
 * the master channel. */
void ikc_master_finalize(ihk_os_t __os)
{
  if (g_ihk_test_mode != TEST_IKC_MASTER_FINALIZE)  // Disable test code
    return ikc_master_finalize_orig(__os);

  unsigned long ivec = 0;
  unsigned long total_branch = 3;

  branch_info_t b_infos[] = {
    { 0, "master channel is already finalized" },
    { 0, "master channel is null" },
    { 0, "main case" },
  };

  dprint_func_enter;

  struct ihk_host_linux_os_data *os = __os;
  int ikc_initialized_prev = os->ikc_initialized;
  int irq_h_count_prev = ihk_os_get_num_handlers(os);

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    int irq_h_count_after = 0;
    os->ikc_initialized = ikc_initialized_prev;

  	if (ivec == 0 || !os->ikc_initialized) {
      os->ikc_initialized = 0;
      if (ivec == 0) goto out;
  		return;
  	}

  	if (ivec == 1 || !os->mchannel) {
      os->ikc_initialized = 0;
      if (ivec == 1) goto out;
    } else if (os->mchannel) {
	    ihk_ikc_destroy_channel(os->mchannel);
    }

  	ihk_ikc_system_exit(os);

  	os->ikc_initialized = 0;

   out:
    irq_h_count_after = ihk_os_get_num_handlers(os);

    OKNG(os->ikc_initialized == 0, "ikc master is released\n");
    if (ivec == total_branch - 1) {
      OKNG(irq_h_count_after == irq_h_count_prev - 1,
           "the number of irq handlers is decreased by 1\n");
      if (os->mchannel) {
        OKNG(!(os->mchannel->flag & IKC_FLAG_ENABLED),
             "master channel is disabled\n");
        OKNG(list_empty(&os->mchannel->packet_pool),
             "packet pool should be empty\n");
      }
    } else {
      OKNG(irq_h_count_after == irq_h_count_prev,
           "the number of irq handlers is unchanged\n");
    }
  }
 err:
  return;
}

/** \brief Get the list of channel (called from IHK-IKC) */
struct list_head *ihk_os_get_ikc_channel_list(ihk_os_t ihk_os)
{
	struct ihk_host_linux_os_data *os = ihk_os;

	return &os->ikc_channels;
}

/** \brief Get the lock for the channel list (called from IHK-IKC) */
spinlock_t *ihk_os_get_ikc_channel_lock(ihk_os_t ihk_os)
{
	struct ihk_host_linux_os_data *os = ihk_os;

	return &os->ikc_channel_lock;
}

/** \brief Get the IKC regular channel (called from IHK-IKC) */
struct ihk_ikc_channel_desc *ihk_os_get_regular_channel(ihk_os_t ihk_os, int cpu)
{
	struct ihk_host_linux_os_data *os = ihk_os;

	return os->regular_channels[cpu];
}

/** \brief Set the IKC regular channel (called from IHK-IKC) */
void ihk_os_set_regular_channel(ihk_os_t ihk_os, struct ihk_ikc_channel_desc *c, int cpu)
{
	struct ihk_host_linux_os_data *os = ihk_os;

	if (cpu < 0 || cpu > num_possible_cpus()) {
		dprintf("%s: WARNING: invalid CPU number: %d\n", __FUNCTION__, cpu);
		return;
	}
	os->regular_channels[cpu] = c;
}

/** \brief Get the interrupt handler of the IKC (called from IHK-IKC) */
struct ihk_host_interrupt_handler *ihk_host_os_get_ikc_handler(ihk_os_t ihk_os)
{
	struct ihk_host_linux_os_data *os = ihk_os;

	return &os->ikc_handler;
}

/** \brief Issue an interrupt to the receiver of the channel
 *  (called from IHK-IKC) */
int ihk_ikc_send_interrupt(struct ihk_ikc_channel_desc *channel)
{
	return ihk_os_issue_interrupt(channel->remote_os,
	                              channel->send.queue->read_cpu,
/* POSTK_DEBUG_ARCH_DEP_10 */
#if defined(__aarch64__)
	                              0x01);
#else
	                              0xd1);
#endif
}

/** \brief Get the lock for the list of listeners (called from IHK-IKC) */
ihk_spinlock_t *ihk_ikc_get_listener_lock(ihk_os_t ihk_os)
{
	struct ihk_host_linux_os_data *os = ihk_os;

	return &os->listener_lock;
}

/** \brief Get the pointer of the specified port in the list of listeners
 * (called from IHK-IKC) */
struct ihk_ikc_listen_param **ihk_ikc_get_listener_entry(ihk_os_t ihk_os,
                                                         int port)
{
	struct ihk_host_linux_os_data *os = ihk_os;

	return &(os->listeners[port]);
}

/** \brief Invokes the master packet handler of the specified kernel
 * (called from IHK-IKC) */
int ihk_ikc_call_master_packet_handler(ihk_os_t ihk_os,
                                       struct ihk_ikc_channel_desc *c,
                                       void *packet)
{
	struct ihk_host_linux_os_data *os = ihk_os;

	if (os->packet_handler) {
		return os->packet_handler(c, packet, os);
	}
	return 0;
}

/** \brief Invokes the master packet handler of the specified kernel
 * (called from IHK-IKC) */
static int arch_master_handler(struct ihk_ikc_channel_desc *c,
                               void *__packet, void *__os)
{
	struct ihk_ikc_master_packet *packet = __packet;

	/* TODO */
	printk("Unhandled master packet! : %x\n", packet->msg);
	return 0;
}

/** \brief Get the wait list for the master channel (Called from IHK-IKC) */
struct list_head *ihk_ikc_get_master_wait_list(ihk_os_t ihk_os)
{
	struct ihk_host_linux_os_data *os = ihk_os;

	return &os->wait_list;
}
/** \brief Get the lock for the wait list for the master channel (called from
 *         IHK-IKC) */
ihk_spinlock_t *ihk_ikc_get_master_wait_lock(ihk_os_t ihk_os)
{
	struct ihk_host_linux_os_data *os = ihk_os;

	return &os->wait_lock;
}

/** \brief Get the master channel of the specified kernel
 *         (Called from IHK-IKC) */
struct ihk_ikc_channel_desc *ihk_os_get_master_channel(ihk_os_t __os)
{
	struct ihk_host_linux_os_data *os = __os;

	return os->mchannel;
}

/** \brief Generate a unique ID for a channel
 *         (Called from IHK-IKC) */
int ihk_os_get_unique_channel_id(ihk_os_t ihk_os)
{
	struct ihk_host_linux_os_data *os = ihk_os;

	return atomic_inc_return(&os->channel_id);
}

/** \brief Initialize the work thread structure
 *         (Called from IHK-IKC) */
void ihk_ikc_linux_init_work_data(ihk_os_t ihk_os,
                                  void (*f)(struct work_struct *))
{
	struct ihk_host_linux_os_data *os = ihk_os;

	os->work_function = f;
}

struct ikc_work_struct {
	struct work_struct work;
	ihk_os_t os;
};

/** \brief Schedule the work thread (Called from IHK-IKC) */
void ihk_ikc_linux_schedule_work(ihk_os_t ihk_os)
{
	struct ihk_host_linux_os_data *os = ihk_os;
	struct ikc_work_struct *work;

	work = kmalloc(sizeof(struct ikc_work_struct), GFP_ATOMIC);
	if (work == NULL) {
		return;
	}
	INIT_WORK(&work->work, os->work_function);
	work->os = ihk_os;
	schedule_work_on(smp_processor_id(), &work->work);
}

/** \brief Get ihk_os_t from the work struct */
ihk_os_t ihk_ikc_linux_get_os_from_work(struct work_struct *work)
{
	struct ikc_work_struct *ikc_work = (struct ikc_work_struct *)work;

	return ikc_work->os;
}
