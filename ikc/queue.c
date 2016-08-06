/**
 * \file ikc/queue.c
 * \brief IHK-IKC: Queue functions
 *
 * Copyright (C) 2011-2012 Taku Shimosawa <shimosawa@is.s.u-tokyo.ac.jp>
 */
#include <ikc/ihk.h>
#include <ikc/queue.h>

//#define DEBUG_QUEUE

#ifdef DEBUG_QUEUE
#define	dkprintf(...)	kprintf(__VA_ARGS__)
#define	ekprintf(...)	kprintf(__VA_ARGS__)
#else
#define dkprintf(...)
#define	ekprintf(...)	kprintf(__VA_ARGS__)
#endif


void ihk_ikc_notify_remote_read(struct ihk_ikc_channel_desc *c);
void ihk_ikc_notify_remote_write(struct ihk_ikc_channel_desc *c);

/*
 * Do copy by long
 */

static void *memcpyl(void *dest, const void *src, size_t n)
{
	unsigned long *d = dest;
	const unsigned long *s = src;

	n /= sizeof(unsigned long);

	while (n > 0) {
		*(d++) = *(s++);
		n--;
	}

	return dest;
}

/*
 * NOTE: Local CPU is responsible to call the init
 */
int ihk_ikc_init_queue(struct ihk_ikc_queue_head *q,
                       int id, int type, int size, int packetsize)
{
	memset(q, 0, sizeof(*q));

	q->id = id;
	q->type = type;
	q->pktsize = packetsize;
	q->pktcount = (size - sizeof(struct ihk_ikc_queue_head)) / packetsize;

	q->read_off = q->write_off = 0;
	q->read_cpu = 0;
	q->write_cpu = 0;
	q->queue_size = q->pktsize * q->pktcount;

	return 0;
}

int ihk_ikc_queue_is_empty(struct ihk_ikc_queue_head *q)
{
	return q->read_off == q->write_off;
}

int ihk_ikc_queue_is_full(struct ihk_ikc_queue_head *q)
{
	uint64_t r, w;

	r = q->read_off;
	w = q->write_off;
	if ((r > w && w + q->pktsize == r)
	    || (r == 0 && w + q->pktsize == q->queue_size)) {
		return 1;
	} else {
		return 0;
	}
}

int ihk_ikc_read_queue(struct ihk_ikc_queue_head *q, void *packet, int flag)
{
	uint64_t o;

	if (ihk_ikc_queue_is_empty(q)) {
		return -1;
	} else{
		memcpyl(packet, (char *)q + sizeof(*q) + q->read_off,
		        q->pktsize);

		o = q->read_off;
		o += q->pktsize;
		if (o >= q->queue_size) {
			o = 0;
		}
		q->read_off = o;
	}
	return 0;
}

int ihk_ikc_read_queue_handler(struct ihk_ikc_queue_head *q, 
                               struct ihk_ikc_channel_desc *c,
                               int (*h)(struct ihk_ikc_channel_desc *,
                                        void *, void *), void *harg, int flag)
{
	uint64_t o;

	if (ihk_ikc_queue_is_empty(q)) {
		return -1;
	} else{
		o = q->read_off;
		o += q->pktsize;
		if (o >= q->queue_size) {
			o = 0;
		}

		h(c, (char *)q + sizeof(*q) + q->read_off, harg);

		q->read_off = o;
	}
	return 0;
}

int ihk_ikc_write_queue(struct ihk_ikc_queue_head *q, void *packet, int flag)
{
	uint64_t o;

	if (ihk_ikc_queue_is_full(q)) {
		return -1;
	} else {
		memcpyl((char *)q + sizeof(*q) + q->write_off,
		        packet, q->pktsize);

		o = q->write_off;
		o += q->pktsize;
		if (o >= q->queue_size) {
			o = 0;
		}
		q->write_off = o;
	}
	return 0;
}

/*
 * Channel and queue descriptors
 */
void ihk_ikc_init_desc(struct ihk_ikc_channel_desc *c,
                       ihk_os_t ros, int port,
                       struct ihk_ikc_queue_head *rq,
                       struct ihk_ikc_queue_head *wq,
                       ihk_ikc_ph_t packet_handler,
					   struct ihk_ikc_channel_desc *master)
{
	struct list_head *channels = ihk_ikc_get_channel_list(ros);
	ihk_spinlock_t *lock = ihk_ikc_get_channel_list_lock(ros);
	unsigned long flags;

	INIT_LIST_HEAD(&c->list);
	INIT_LIST_HEAD(&c->all_list);

	c->remote_os = ros;
	c->port = port;
	c->channel_id = ihk_ikc_get_unique_channel_id(ros);
	c->recv.queue = rq;
	c->send.queue = wq;
	if (rq) {
		c->recv.queue->channel_id = c->channel_id;
		c->recv.queue->read_cpu = ihk_ikc_get_processor_id();
		c->recv.cache = *rq;
	}
	if (wq) {
		c->remote_channel_id = c->send.cache.channel_id;
		c->send.queue->write_cpu = ihk_ikc_get_processor_id();
		c->send.cache = *wq;
	}
	c->handler = packet_handler;
	c->master = master;

	ihk_ikc_spinlock_init(&c->recv.lock);
	ihk_ikc_spinlock_init(&c->send.lock);

	flags = ihk_ikc_spinlock_lock(lock);
	list_add_tail(&c->list, channels);
	ihk_ikc_spinlock_unlock(lock, flags);
}

void ihk_ikc_channel_set_cpu(struct ihk_ikc_channel_desc *c, int cpu)
{
	c->send.queue->write_cpu = c->recv.queue->read_cpu = cpu;
}

int ihk_ikc_set_remote_queue(struct ihk_ikc_queue_desc *q, ihk_os_t os,
                             unsigned long rphys, unsigned long qsize)
{
	int qpages;

	qpages = (qsize + PAGE_SIZE - 1) >> PAGE_SHIFT;

	ihk_ikc_spinlock_init(&q->lock);
	q->qrphys = rphys;
	q->qphys = ihk_ikc_map_memory(os, q->qrphys, qpages * PAGE_SIZE);
	q->queue = ihk_ikc_map_virtual(ihk_os_to_dev(os), q->qphys,
	                               qpages,
	                               IHK_IKC_QUEUE_PT_ATTR);
	q->cache = *q->queue;

	return 0;
}

struct ihk_ikc_channel_desc *ihk_ikc_create_channel(ihk_os_t os,
                                                    int port,
                                                    int packet_size,
                                                    unsigned long qsize,
                                                    unsigned long *rq,
                                                    unsigned long *sq,
                                                    enum ihk_ikc_channel_flag f)
{
	unsigned long phys;
	struct ihk_ikc_channel_desc *desc;
	struct ihk_ikc_queue_head *recvq, *sendq;
	int qpages;

	qpages = (qsize + PAGE_SIZE - 1) >> PAGE_SHIFT;

	desc = ihk_ikc_malloc(sizeof(struct ihk_ikc_channel_desc)
	                      + packet_size);
	if (!desc) {
		return NULL;
	}

	memset(desc, 0, sizeof(*desc));

	desc->flag = f;

	if (!*rq) {
		recvq = ihk_ikc_alloc_queue(qpages);
		if (!recvq) {
			return NULL;
		}

		ihk_ikc_init_queue(recvq, 1, port, PAGE_SIZE * qpages,
		                   packet_size);
		*rq = virt_to_phys(recvq);

		desc->recv.qrphys = 0;
		desc->recv.qphys = *rq;
	} else {
		phys = ihk_ikc_map_memory(os, *rq, qpages * PAGE_SIZE);
		recvq = ihk_ikc_map_virtual(ihk_os_to_dev(os), phys,
		                            qpages,
		                            IHK_IKC_QUEUE_PT_ATTR);

		desc->recv.qrphys = *rq;
		desc->recv.qphys = phys;
	}
	/* XXX: This do not assume local send queue */
	if (*sq) {
		phys = ihk_ikc_map_memory(os, *sq, qpages * PAGE_SIZE);
		sendq = ihk_ikc_map_virtual(ihk_os_to_dev(os), phys,
		                            qpages,
		                            IHK_IKC_QUEUE_PT_ATTR);

		desc->send.qrphys = *sq;
		desc->send.qphys = phys;
	} else {
		sendq = NULL;
	}

	ihk_ikc_init_desc(desc, os, port, recvq, sendq, NULL,
			ihk_ikc_get_master_channel(os));

	return desc;
}

void ihk_ikc_free_channel(struct ihk_ikc_channel_desc *desc)
{
	ihk_os_t os = desc->remote_os;
	int qpages;
	ihk_spinlock_t *lock = ihk_ikc_get_channel_list_lock(os);
	unsigned long flags;

	flags = ihk_ikc_spinlock_lock(lock);
	list_del(&desc->list);
	ihk_ikc_spinlock_unlock(lock, flags);

	if (desc->recv.queue) {
		qpages = (desc->recv.queue->queue_size
		          + sizeof(struct ihk_ikc_queue_head) + PAGE_SIZE - 1)
			>> PAGE_SHIFT;
		if (desc->recv.qrphys) {
			ihk_ikc_unmap_virtual(ihk_os_to_dev(os),
			                      desc->recv.queue,
			                      qpages);
			ihk_ikc_unmap_memory(os, desc->recv.qphys, qpages);
		} else {
			ihk_ikc_free_queue(desc->recv.queue);
		}
	}

	if (desc->send.queue) {
		qpages = (desc->send.queue->queue_size
		          + sizeof(struct ihk_ikc_queue_head) + PAGE_SIZE - 1)
			>> PAGE_SHIFT;
		if (desc->send.qrphys) {
			ihk_ikc_unmap_virtual(ihk_os_to_dev(os),
			                      desc->send.queue,
			                      qpages);
			ihk_ikc_unmap_memory(os, desc->send.qphys, qpages);
		} else {
			ihk_ikc_free_queue(desc->send.queue);
		}
	}

	ihk_ikc_free(desc);
}


int ihk_ikc_recv(struct ihk_ikc_channel_desc *channel, void *p, int opt)
{
	int r;
	unsigned long flags;

#ifdef IHK_OS_MANYCORE
	flags = cpu_disable_interrupt_save();
#else
	local_irq_save(flags);
#endif
	if (ihk_ikc_channel_enabled(channel)) {
		r = ihk_ikc_read_queue(channel->recv.queue, p, opt);
		/* XXX: Optimal interrupt */
		if (!(opt & IKC_NO_NOTIFY)) {
			ihk_ikc_notify_remote_read(channel);
		}
	} else {
		r = -EINVAL;
	}
#ifdef IHK_OS_MANYCORE
	cpu_restore_interrupt(flags);
#else
	local_irq_restore(flags);
#endif

	return r;
}

static int __ihk_ikc_recv_nocopy(struct ihk_ikc_channel_desc *channel,
                                 ihk_ikc_ph_t h, void *harg, int opt)
{
	unsigned long flags;
	int r;

	flags = ihk_ikc_spinlock_lock(&channel->recv.lock);
	if (ihk_ikc_channel_enabled(channel) &&
	    !ihk_ikc_queue_is_empty(channel->recv.queue)) {
		while (ihk_ikc_read_queue_handler(channel->recv.queue,
		                                  channel,
		                                  h, harg, opt) == 0);
		/* XXX: Optimal interrupt */
		ihk_ikc_notify_remote_read(channel);

		r = 0;
	} else {
		r = -EINVAL;
	}
	ihk_ikc_spinlock_unlock(&channel->recv.lock, flags);

	return r;
}

int ihk_ikc_recv_handler(struct ihk_ikc_channel_desc *channel, 
		ihk_ikc_ph_t h, void *harg, int opt)
{
	int r = -ENOENT;
	char *p = ihk_ikc_malloc(channel->recv.queue->pktsize);

	if (!p) {
		kprintf("%s: error allocating packet\n", __FUNCTION__);
		return -ENOMEM;
	}

	if ((r = ihk_ikc_recv(channel, p, opt | IKC_NO_NOTIFY)) != 0) {
		ihk_ikc_free(p);
		goto out;
	}

	/* XXX: Reference to the packet must not be stored by handler */
	h(channel, p, harg);

	ihk_ikc_free(p);

	if (channel->flag & IKC_FLAG_NO_COPY) {
		ihk_ikc_notify_remote_read(channel);
	}
out:
	return r;
}

void ihk_ikc_notify_remote_read(struct ihk_ikc_channel_desc *c)
{
	ihk_ikc_send_interrupt(c);
}
void ihk_ikc_notify_remote_write(struct ihk_ikc_channel_desc *c)
{
	ihk_ikc_send_interrupt(c);
}

void __ihk_ikc_enable_channel(struct ihk_ikc_channel_desc *channel)
{
	channel->flag |= IKC_FLAG_ENABLED;
}

void __ihk_ikc_disable_channel(struct ihk_ikc_channel_desc *channel)
{
	channel->flag &= ~IKC_FLAG_ENABLED;
}

void ihk_ikc_enable_channel(struct ihk_ikc_channel_desc *channel)
{
	unsigned long flags;

	dkprintf("Channel %d enabled. Recv CPU = %d.\n",
	        channel->channel_id, channel->send.queue->read_cpu);

	flags = ihk_ikc_spinlock_lock(&channel->recv.lock);
	__ihk_ikc_enable_channel(channel);
	ihk_ikc_spinlock_unlock(&channel->recv.lock, flags);
}

void ihk_ikc_disable_channel(struct ihk_ikc_channel_desc *channel)
{
	unsigned long flags;

	flags = ihk_ikc_spinlock_lock(&channel->recv.lock);
	__ihk_ikc_disable_channel(channel);
	ihk_ikc_spinlock_unlock(&channel->recv.lock, flags);
}

struct ihk_ikc_channel_desc *ihk_ikc_find_channel(ihk_os_t os, int id)
{
	ihk_spinlock_t *lock = ihk_ikc_get_channel_list_lock(os);
	struct list_head *channels = ihk_ikc_get_channel_list(os);
	struct ihk_ikc_channel_desc *c;
	unsigned long flags;

	flags = ihk_ikc_spinlock_lock(lock);
	list_for_each_entry(c, channels, list) {
		if (c->channel_id == id) {
			ihk_ikc_spinlock_unlock(lock, flags);
			return c;
		}
	}
	ihk_ikc_spinlock_unlock(lock, flags);

	return NULL;
}

IHK_EXPORT_SYMBOL(ihk_ikc_recv);
IHK_EXPORT_SYMBOL(ihk_ikc_recv_handler);
IHK_EXPORT_SYMBOL(ihk_ikc_enable_channel);
IHK_EXPORT_SYMBOL(ihk_ikc_disable_channel);
IHK_EXPORT_SYMBOL(ihk_ikc_free_channel);
IHK_EXPORT_SYMBOL(ihk_ikc_find_channel);
IHK_EXPORT_SYMBOL(ihk_ikc_channel_set_cpu);

