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

void ihk_ikc_linux_init_work_data(ihk_os_t ihk_os,
                                  void (*f)(struct work_struct *));
void ihk_ikc_linux_schedule_work(ihk_os_t ihk_os);
ihk_os_t ihk_ikc_linux_get_os_from_work(struct work_struct *work);

static void __ihk_ikc_reception_handler(ihk_os_t os)
{
	struct ihk_ikc_channel_desc *m_channel;
	struct ihk_ikc_channel_desc *r_channel;

	if (smp_processor_id() == 0) {
		m_channel = ihk_ikc_get_master_channel(os);
		while (ihk_ikc_channel_enabled(m_channel) &&
		       !ihk_ikc_queue_is_empty(m_channel->recv.queue)) {
			ihk_ikc_recv_handler(m_channel, m_channel->handler, os, 0);
		}
	}

	r_channel = ihk_ikc_get_regular_channel(os, smp_processor_id());
	if (!r_channel) {
		/* It is fine not to have this channel for CPU 0 as we may be
		 * in initialization phase where only master channel exists yet.
		 * Otherwise, print a warning */
		if (smp_processor_id() > 0) {
			printk("%s: WARNING: r_channel for CPU %d does not exist\n",
					__FUNCTION__, smp_processor_id());
		}
		return;
	}
	while (ihk_ikc_channel_enabled(r_channel) &&
	       !ihk_ikc_queue_is_empty(r_channel->recv.queue)) {
		ihk_ikc_recv_handler(r_channel, r_channel->handler, os, 0);
	}
}

/** \brief Worker thread for IKC interrupts */
static void ikc_work_func(struct work_struct *work)
{
	ihk_os_t os = ihk_ikc_linux_get_os_from_work(work);
	__ihk_ikc_reception_handler(os);
}

/** \brief IKC interrupt handler (interrupt context) */
static void ihk_ikc_interrupt_handler(ihk_os_t os, void *os_priv, void *priv)
{
#ifdef IHK_IKC_RECV_HANDLER_IN_WORKQ
	ihk_ikc_linux_schedule_work(priv);
#else
	/*
	 * Pass packets to mcexec threads directly from IRQ context.
	 * Implications: we must use GFP_ATOMIC in all allocations and
	 * cannot sleep on semaphores, etc.
	 * This buys us ~10000 cycles latency on the KNL.
	 */
	__ihk_ikc_reception_handler(os);
#endif
}

/** \brief Get the master channel for an OS */
struct ihk_ikc_channel_desc *ihk_ikc_get_master_channel(ihk_os_t os)
{
	return ihk_os_get_master_channel(os);
}

/** \brief Initialize the IKC stuffs of an OS */
void ihk_ikc_system_init(ihk_os_t os)
{
	struct ihk_host_interrupt_handler *h;
	
	h = ihk_host_os_get_ikc_handler(os);
	
	INIT_LIST_HEAD(&h->list);
	h->func = ihk_ikc_interrupt_handler;
	h->priv = os;

	ihk_ikc_linux_init_work_data(os, ikc_work_func);
	ihk_os_register_interrupt_handler(os, 0, h);
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

