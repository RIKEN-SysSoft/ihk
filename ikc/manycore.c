/**
 * \file ikc/manycore.c
 * \brief IHK-IKC: Wrapper functions in IHK-Manycore for IHK-IKC
 *
 * Copyright (C) 2011-2012 Taku Shimosawa <shimosawa@is.s.u-tokyo.ac.jp>
 */
#include <ikc/ihk.h>
#include <ikc/queue.h>
#include <ikc/master.h>

struct ihk_ikc_channel_desc *ihk_mc_get_master_channel(void);

static ihk_spinlock_t ihk_ikc_channels_lock;
static LIST_HEAD(ihk_ikc_channels);

struct list_head *ihk_ikc_get_channel_list(ihk_os_t os)
{
	return &ihk_ikc_channels;
}
ihk_spinlock_t *ihk_ikc_get_channel_list_lock(ihk_os_t os)
{
	return &ihk_ikc_channels_lock;
}

static void ihk_ikc_interrupt_handler(void *priv)
{
	/* This should be done in the software irq... */
	struct ihk_ikc_channel_desc *c;
	unsigned long flags;

	/* XXX: Linear search?, Per-core list of the queues! */
	flags = ihk_ikc_spinlock_lock(&ihk_ikc_channels_lock);
	list_for_each_entry(c, &ihk_ikc_channels, list) {
		if (ihk_ikc_channel_enabled(c) && 
		    !ihk_ikc_queue_is_empty(c->recv.queue) &&
		    c->recv.queue->read_cpu == ihk_mc_get_processor_id()) {
			ihk_ikc_spinlock_unlock(&ihk_ikc_channels_lock, flags);
			ihk_ikc_recv_handler(c, c->handler, NULL, 0);
			flags = ihk_ikc_spinlock_lock(&ihk_ikc_channels_lock);
		}
	}
	ihk_ikc_spinlock_unlock(&ihk_ikc_channels_lock, flags);
}
struct ihk_ikc_channel_desc *ihk_ikc_get_master_channel(ihk_os_t os)
{
	return ihk_mc_get_master_channel();
}

static struct ihk_mc_interrupt_handler ihk_ikc_handler = {
	.func = ihk_ikc_interrupt_handler,
	.priv = NULL,
};

void ihk_ikc_system_init(ihk_os_t os)
{
	INIT_LIST_HEAD(&ihk_ikc_handler.list);
	ihk_mc_register_interrupt_handler(ihk_mc_get_vector(IHK_GV_IKC),
	                                  &ihk_ikc_handler);
}

void ihk_ikc_system_exit(ihk_os_t os)
{
	ihk_mc_unregister_interrupt_handler(ihk_mc_get_vector(IHK_GV_IKC),
	                                    &ihk_ikc_handler);
}

struct ihk_ikc_queue_head *ihk_ikc_alloc_queue(int qpages)
{
	return ihk_mc_alloc_pages(qpages, IHK_MC_AP_CRITICAL);
}

void ihk_ikc_free_queue(struct ihk_ikc_queue_head *q)
{
	ihk_mc_free_pages(q, (sizeof(struct ihk_ikc_queue_head) + 
	                      q->queue_size + PAGE_SIZE - 1) >> PAGE_SHIFT);
}

void *ihk_ikc_malloc(int size)
{
	return ihk_mc_allocate(size, IHK_MC_AP_CRITICAL);
}
void ihk_ikc_free(void *p)
{
	return ihk_mc_free(p);
}

extern ihk_ikc_ph_t arch_master_channel_packet_handler;

int call_arch_master_packet_handler(void *os, struct ihk_ikc_channel_desc *c,
                                    void *__packet)
{
	return arch_master_channel_packet_handler(c, __packet, os);
}

static LIST_HEAD(wait_list);
static ihk_spinlock_t wait_lock;

struct list_head *ihk_ikc_get_master_wait_list(ihk_os_t ihk_os)
{
	return &wait_list;
}

ihk_spinlock_t *ihk_ikc_get_master_wait_lock(ihk_os_t ihk_os)
{
	return &wait_lock;
}

void ihk_ikc_wait_init(ihk_wait_t *wait)
{
}

int ihk_ikc_wait_master(struct ihk_ikc_master_wait_struct *ws)
{
	/* XXX: SPINNING! */
	while (!ws->status) {
		cpu_pause();
		barrier();
	}
	return 0;
}

void ihk_ikc_wake_master(struct ihk_ikc_master_wait_struct *ws)
{
	ws->status = 1;
}

static ihk_atomic_t channel_id;

int ihk_ikc_get_unique_channel_id(ihk_os_t ihk_os)
{
	return ihk_atomic_inc_return(&channel_id);
}
