/**
 * \file ikc/manycore.c
 * \brief IHK-IKC: Wrapper functions in IHK-Manycore for IHK-IKC
 *
 * Copyright (C) 2011-2012 Taku Shimosawa <shimosawa@is.s.u-tokyo.ac.jp>
 */
#include <ikc/ihk.h>
#include <ikc/queue.h>
#include <ikc/master.h>

extern int num_processors;

struct ihk_ikc_channel_desc *ihk_mc_get_master_channel(void);

static ihk_spinlock_t *ihk_ikc_channels_lock;
static struct list_head *ihk_ikc_channels;

static struct ihk_ikc_channel_desc **regular_channels;

struct list_head *ihk_ikc_get_channel_list(ihk_os_t os)
{
	return &ihk_ikc_channels[ihk_mc_get_processor_id()];
}
ihk_spinlock_t *ihk_ikc_get_channel_list_lock(ihk_os_t os)
{
	return &ihk_ikc_channels_lock[ihk_mc_get_processor_id()];
}

struct ihk_ikc_channel_desc *ihk_ikc_get_regular_channel(ihk_os_t os, int cpu)
{
	if (cpu < 0 || cpu >= num_processors) {
		return NULL;
	}

	return regular_channels[cpu];
}

void ihk_ikc_set_regular_channel(ihk_os_t os, struct ihk_ikc_channel_desc *c, int cpu)
{
	if (cpu >= 0 && cpu < num_processors) {
		regular_channels[cpu] = c;
	}
}

static void ihk_ikc_interrupt_handler(void *priv)
{
	/* This should be done in the software irq... */
	struct ihk_ikc_channel_desc *m_channel;
	struct ihk_ikc_channel_desc *r_channel;

	if (ihk_mc_get_processor_id() == 0) {
		m_channel = ihk_ikc_get_master_channel(NULL);
		while (ihk_ikc_channel_enabled(m_channel) &&
		       !ihk_ikc_queue_is_empty(m_channel->recv.queue) &&
		       m_channel->recv.queue->read_cpu == ihk_mc_get_processor_id()) {
			ihk_ikc_recv_handler(m_channel, m_channel->handler, NULL, 0);
		}
	}

	r_channel = ihk_ikc_get_regular_channel(NULL, ihk_mc_get_processor_id());
	while (ihk_ikc_channel_enabled(r_channel) &&
	       !ihk_ikc_queue_is_empty(r_channel->recv.queue) &&
	       r_channel->recv.queue->read_cpu == ihk_mc_get_processor_id()) {
		ihk_ikc_recv_handler(r_channel, r_channel->handler, NULL, 0);
	}
}

int ihk_ikc_send(struct ihk_ikc_channel_desc *channel, void *p, int opt)
{
	int r;
	unsigned long flags;

	if(!channel || !p)
		return -EINVAL;

	flags = cpu_disable_interrupt_save();

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

	cpu_restore_interrupt(flags);

	return r;
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
	int i;
	INIT_LIST_HEAD(&ihk_ikc_handler.list);
	ihk_mc_register_interrupt_handler(ihk_mc_get_vector(IHK_GV_IKC),
	                                  &ihk_ikc_handler);

	ihk_ikc_channels = ihk_ikc_malloc(sizeof(*ihk_ikc_channels) * num_processors);
	ihk_ikc_channels_lock = ihk_ikc_malloc(sizeof(*ihk_ikc_channels_lock) * num_processors);

	regular_channels = ihk_ikc_malloc(sizeof(*regular_channels) * num_processors);

	if (!ihk_ikc_channels || !ihk_ikc_channels_lock || !regular_channels) {
		kprintf("%s: error allocating channels list\n", __FUNCTION__);
		panic("");
	}

	memset(regular_channels, 0, sizeof(*regular_channels) * num_processors);

	for (i = 0; i < num_processors; ++i) {
		INIT_LIST_HEAD(&ihk_ikc_channels[i]);
		ihk_ikc_spinlock_init(&ihk_ikc_channels_lock[i]);
	}
}

void ihk_ikc_system_exit(ihk_os_t os)
{
	ihk_mc_unregister_interrupt_handler(ihk_mc_get_vector(IHK_GV_IKC),
	                                    &ihk_ikc_handler);
}

struct ihk_ikc_queue_head *ihk_ikc_alloc_queue(int qpages)
{
	return ihk_mc_alloc_pages(qpages, 0);
}

void ihk_ikc_free_queue(struct ihk_ikc_queue_head *q)
{
	ihk_mc_free_pages(q, (sizeof(struct ihk_ikc_queue_head) + 
	                      q->queue_size + PAGE_SIZE - 1) >> PAGE_SHIFT);
}

void *ihk_ikc_malloc(int size)
{
	return ihk_mc_allocate(size, 0);
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
