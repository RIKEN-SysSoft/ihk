/**
 * \file ikc/linux.c
 * \brief IHK-IKC: Wrapper functions in IHK-Host in Linux for IHK-IKC
 *
 * Copyright (C) 2011-2012 Taku Shimosawa <shimosawa@is.s.u-tokyo.ac.jp>
 */
#include <ikc/ihk.h>
#include <ikc/master.h>
#include <linux/slab.h>
#include <asm/bitops.h>
#include <asm/smp.h>
#include <linux/interrupt.h>

//#define IHK_IKC_RECV_HANDLER_IN_WORKQ

extern struct list_head *ihk_host_os_get_ikc_channel_list(ihk_os_t ihk_os);
struct ihk_host_interrupt_handler *ihk_host_os_get_ikc_handler(ihk_os_t ihk_os);
int ihk_ikc_call_master_packet_handler(ihk_os_t ihk_os,
                                       struct ihk_ikc_channel_desc *c,
                                       void *packet);
struct ihk_ikc_channel_desc *ihk_os_get_master_channel(ihk_os_t __os);
int ihk_os_get_ikc_irq(ihk_os_t os, int itype);

void ihk_ikc_linux_init_work_data(ihk_os_t ihk_os,
                                  void (*f)(struct work_struct *));
void ihk_ikc_linux_schedule_work(ihk_os_t ihk_os);
ihk_os_t ihk_ikc_linux_get_os_from_work(struct work_struct *work);

/*
 * Pass packets to mcexec threads directly from IRQ context.
 * Implications: we must use GFP_ATOMIC in all allocations and
 * cannot sleep on semaphores, etc.
 * This buys us ~10000 cycles latency on the KNL.
 */
static void __ihk_ikc_reception_handler(ihk_os_t os, void *os_priv, void *priv, int irq)
{
	struct ihk_ikc_channel_desc *recv_channel;

	if (irq == ihk_os_get_ikc_irq(os, INTR_TYPE_IKC_MASTER)) {
		recv_channel = ihk_ikc_get_master_channel(os);
	}
	else if (irq == ihk_os_get_ikc_irq(os, INTR_TYPE_IKC_REGULAR)) {
		recv_channel = ihk_ikc_get_regular_channel(os, smp_processor_id());
	}
	else
		return;

	while (ihk_ikc_channel_enabled(recv_channel) &&
  	       !ihk_ikc_queue_is_empty(recv_channel->recv.queue)) {
		ihk_ikc_recv_handler(recv_channel, recv_channel->handler, os, 0);
	}
}

/** \brief Get the master channel for an OS */
struct ihk_ikc_channel_desc *ihk_ikc_get_master_channel(ihk_os_t os)
{
	return ihk_os_get_master_channel(os);
}

/** \brief Initialize the IKC stuffs of an OS */
void ihk_ikc_system_init(ihk_os_t os)
{
	struct ihk_host_interrupt_handler *ikc_h;
	
	ikc_h = ihk_host_os_get_ikc_handler(os);

	ikc_h->func = __ihk_ikc_reception_handler;
	ikc_h->priv = os;
	
	ihk_os_register_interrupt_handler(os, 0, ikc_h);
}

void ihk_ikc_system_exit(ihk_os_t os)
{
	struct ihk_host_interrupt_handler *h;
	
	h = ihk_host_os_get_ikc_handler(os);
	ihk_os_unregister_interrupt_handler(os, 0, h);
}

struct ihk_ikc_queue_head *ihk_ikc_alloc_queue(int qpages)
{
	int order = fls(qpages) - 1;

	return (void *)__get_free_pages(in_interrupt() ? GFP_ATOMIC : GFP_KERNEL, order);
}

void ihk_ikc_free_queue(struct ihk_ikc_queue_head *q)
{
	int qpages = (q->queue_size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	int order = fls(qpages) - 1;

	free_pages((unsigned long)q, order);
}

void *ihk_ikc_malloc(int size)
{
	return kmalloc(size, GFP_ATOMIC);
}
void ihk_ikc_free(void *p)
{
	kfree(p);
}

int call_arch_master_packet_handler(void *os, struct ihk_ikc_channel_desc *c,
                                    void *__packet)
{
	return ihk_ikc_call_master_packet_handler(os, c, __packet);
}

void ihk_ikc_wait_init(ihk_wait_t *wait)
{
	init_waitqueue_head(wait);
}

int ihk_ikc_wait_master(struct ihk_ikc_master_wait_struct *ws)
{
	return wait_event_interruptible(ws->wait, ws->status);
}

void ihk_ikc_wake_master(struct ihk_ikc_master_wait_struct *ws)
{
	wake_up_interruptible(&ws->wait);
}

int ihk_ikc_send(struct ihk_ikc_channel_desc *channel, void *p, int opt)
{
	int r;
	unsigned long flags;

	if(!channel || !p)
		return -EINVAL;

	local_irq_save(flags);
retry:
	/* Add main packet to target channel */
	if (ihk_ikc_channel_enabled(channel)) {
		r = ihk_ikc_write_queue(channel->send.queue, p, opt);

		if (r != 0) {
			kprintf("%s: couldn't append packet -> retrying\n", __FUNCTION__);
			goto retry;
		}

		if (!(opt & IKC_NO_NOTIFY)) {
			ihk_ikc_notify_remote_write(channel);
		}
	} else {
		r = -EINVAL;
	}

	local_irq_restore(flags);
	return r;
}

IHK_EXPORT_SYMBOL(ihk_ikc_send);

