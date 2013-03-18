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

/** \brief Worker thread for IKC interrupts */
static void ikc_work_func(struct work_struct *work)
{
	struct ihk_ikc_channel_desc *c;
	struct list_head *channels;
	ihk_os_t os = ihk_ikc_linux_get_os_from_work(work);
	ihk_spinlock_t *lock;
	unsigned long flags;

	channels = ihk_ikc_get_channel_list(os);
	lock = ihk_ikc_get_channel_list_lock(os);

	/* XXX: Linear search? */
	flags = ihk_ikc_spinlock_lock(lock);
	list_for_each_entry(c, channels, list) {
		if (ihk_ikc_channel_enabled(c) && 
		    !ihk_ikc_queue_is_empty(c->recv.queue)) {
			ihk_ikc_spinlock_unlock(lock, flags);
			ihk_ikc_recv_handler(c, c->handler, os, 0);
			flags = ihk_ikc_spinlock_lock(lock);
		}
	}
	ihk_ikc_spinlock_unlock(lock, flags);
}

/** \brief IKC interrupt handler (interrupt context) */
static void ihk_ikc_interrupt_handler(ihk_os_t os, void *os_priv, void *priv)
{
	/* This should be done in the software irq... */
	ihk_ikc_linux_schedule_work(priv);
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

	return (void *)__get_free_pages(GFP_KERNEL | GFP_ATOMIC, order);
}

void ihk_ikc_free_queue(struct ihk_ikc_queue_head *q)
{
	int qpages = (q->queue_size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	int order = fls(qpages) - 1;

	free_pages((unsigned long)q, order);
}

void *ihk_ikc_malloc(int size)
{
	return kmalloc(size, GFP_KERNEL | GFP_ATOMIC);
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

