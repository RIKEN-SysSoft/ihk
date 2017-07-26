/**
 * \file ikc/queue.c
 * \brief IHK-IKC: Queue functions
 *
 * Copyright (C) 2011-2012 Taku Shimosawa <shimosawa@is.s.u-tokyo.ac.jp>
 * Lock-free implementation: Balazs Gerofi <bgerofi@riken.jp>
 */
#include <ikc/ihk.h>
#include <ikc/queue.h>
#include <ikc/msg.h>

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
	if (!q) {
		return -EINVAL;
	}

	memset(q, 0, sizeof(*q));

	q->id = id;
	q->type = type;
	q->pktsize = packetsize;
	q->pktcount = (size - sizeof(struct ihk_ikc_queue_head)) / packetsize;

	q->read_off = q->max_read_off = q->write_off = 0;
	q->read_cpu = 0;
	q->write_cpu = 0;
	q->queue_size = q->pktsize * q->pktcount;
	dkprintf("%s: queue %p pktcount: %lu\n",
		__FUNCTION__, virt_to_phys(q), q->pktcount);

	return 0;
}

int ihk_ikc_queue_is_empty(struct ihk_ikc_queue_head *q)
{
	if (!q) {
		return -EINVAL;
	}
	return q->read_off == q->max_read_off;
}

int ihk_ikc_queue_is_full(struct ihk_ikc_queue_head *q)
{
	uint64_t r, w;

	if (!q) {
		return -EINVAL;
	}

	r = q->read_off;
	w = q->write_off;

	barrier();

	if ((w - r) == q->pktcount)
		return 1;

	return 0;
}

int ihk_ikc_read_queue(struct ihk_ikc_queue_head *q, void *packet, int flag)
{
	uint64_t r, m;

	if(!q || !packet) {
		return -EINVAL;
	}

retry:
	r = q->read_off;
	m = q->max_read_off;

	/* Is the queue empty? */
	if (r == m) {
		return -1;
	}

	/* Try to advance the queue, but see if someone else has done it already */
	if (!__sync_bool_compare_and_swap(&q->read_off, r, r + 1)) {
		goto retry;
	}
	dkprintf("%s: queue %p r: %lu, m: %lu\n",
			__FUNCTION__, virt_to_phys(q), r, m);

	memcpyl(packet, (char *)q + sizeof(*q) + ((r % q->pktcount) * q->pktsize),
			q->pktsize);

	return 0;
}

int ihk_ikc_read_queue_handler(struct ihk_ikc_queue_head *q, 
                               struct ihk_ikc_channel_desc *c,
                               int (*h)(struct ihk_ikc_channel_desc *,
                                        void *, void *), void *harg, int flag)
{
	uint64_t r, m;

retry:
	r = q->read_off;
	m = q->max_read_off;
	barrier();

	/* Is the queue empty? */
	if (r == m) {
		return -1;
	}

	/* Try to advance the queue, but see if someone else has done it already */
	if (!__sync_bool_compare_and_swap(&q->read_off, r, r + 1)) {
		goto retry;
	}
	dkprintf("%s: queue %p r: %lu, m: %lu\n",
			__FUNCTION__, virt_to_phys(q), r, m);

	h(c, (char *)q + sizeof(*q) + ((r % q->pktcount) * q->pktsize), harg);

	return 0;
}

int ihk_ikc_write_queue(struct ihk_ikc_queue_head *q, void *packet, int flag)
{
	uint64_t r, w;

	if(!q || !packet) {
		return -EINVAL;
	}
retry:
	r = q->read_off;
	w = q->write_off;
	barrier();

	/* Is the queue full? */
	if ((w - r) == q->pktcount) {
		dkprintf("%s: queue %p r: %lu, w: %lu full, retrying\n",
			__FUNCTION__, virt_to_phys(q), r, w);
		goto retry;
	}

	/* Try to advance the queue, but see if someone else has done it already */
	if (!__sync_bool_compare_and_swap(&q->write_off, w, w + 1)) {
		goto retry;
	}
	dkprintf("%s: queue %p r: %lu, w: %lu\n",
			__FUNCTION__, virt_to_phys(q), r, w);

	memcpyl((char *)q + sizeof(*q) + ((w % q->pktcount) * q->pktsize),
			packet, q->pktsize);

	/*
	 * Advance the max read index so that the element is visible to readers,
	 * this has to succeed eventually, but we cannot afford to be interrupted
	 * by another request which would then end up waiting for this hence
	 * IRQs are disabled during queue operations.
	 */
	while (!__sync_bool_compare_and_swap(&q->max_read_off, w, w + 1)) {}

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
	struct list_head *all_list = ihk_ikc_get_channel_list(ros);
	ihk_spinlock_t *all_lock = ihk_ikc_get_channel_list_lock(ros);
	unsigned long flags;

	INIT_LIST_HEAD(&c->list_all);
	INIT_LIST_HEAD(&c->packet_pool);

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
	ihk_ikc_spinlock_init(&c->packet_pool_lock);

	flags = ihk_ikc_spinlock_lock(all_lock);
	list_add_tail(&c->list_all, all_list);
	ihk_ikc_spinlock_unlock(all_lock, flags);
}

/*
 * Packet pool functions.
 */
struct ihk_ikc_free_packet *ihk_ikc_alloc_packet(
	struct ihk_ikc_channel_desc *c)
{
	unsigned long flags;
	struct ihk_ikc_free_packet *p = NULL;
	struct ihk_ikc_free_packet *p_iter;

	flags = ihk_ikc_spinlock_lock(&c->packet_pool_lock);
	list_for_each_entry(p_iter, &c->packet_pool, list) {
		p = p_iter;
		list_del(&p->list);
		break;
	}

	/* No packet? Allocate new */
	if (!p) {
retry_alloc:
		p = (struct ihk_ikc_free_packet *)ihk_ikc_malloc(c->recv.queue->pktsize);
		if (!p) {
			kprintf("%s: ERROR allocating packet, retrying\n", __FUNCTION__);
			goto retry_alloc;
		}
		dkprintf("%s: packet %p kmalloc'd on channel %p %s\n",
			__FUNCTION__, p, c, c == c->master ? "(master)" : "");
	}
	else {
		dkprintf("%s: packet %p obtained from pool on channel %p %s\n",
			__FUNCTION__, p, c, c == c->master ? "(master)" : "");
	}

	ihk_ikc_spinlock_unlock(&c->packet_pool_lock, flags);
	return p;
}

void ihk_ikc_release_packet(struct ihk_ikc_free_packet *p, struct ihk_ikc_channel_desc *c)
{
	unsigned long flags;

	if (!p) {
		return;
	}

	if (!c) {
		kprintf("%s: WARNING: can't release on NULL channel\n",
				__FUNCTION__);
		ihk_ikc_free(p);
		return;
	}

	flags = ihk_ikc_spinlock_lock(&c->packet_pool_lock);
	list_add_tail(&p->list, &c->packet_pool);
	ihk_ikc_spinlock_unlock(&c->packet_pool_lock, flags);
	dkprintf("%s: packet %p released to pool on channel %p %s\n",
			__FUNCTION__, p, c, c == c->master ? "(master)" : "");
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
	struct ihk_ikc_free_packet *p_iter, *p_next;
	unsigned long flags;

	flags = ihk_ikc_spinlock_lock(lock);
	list_del(&desc->list_all);
	ihk_ikc_spinlock_unlock(lock, flags);

	flags = ihk_ikc_spinlock_lock(&desc->packet_pool_lock);
	list_for_each_entry_safe(p_iter, p_next, &desc->packet_pool, list) {
		list_del(&p_iter->list);
		ihk_ikc_free(p_iter);
	}
	ihk_ikc_spinlock_unlock(&desc->packet_pool_lock, flags);

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

	if (!channel || !p) {
		return -EINVAL;
	}

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

#if 0
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
#endif

int ihk_ikc_recv_handler(struct ihk_ikc_channel_desc *channel, 
		ihk_ikc_ph_t h, void *harg, int opt)
{
	char *p = NULL;
	int r = -ENOENT;

	if (!channel) {
		return -EINVAL;
	}

	/* Get free packet from channel pool */
	p = (char *)ihk_ikc_alloc_packet(channel);

	if (!p) {
		kprintf("%s: error allocating packet\n", __FUNCTION__);
		return -ENOMEM;
	}

	if ((r = ihk_ikc_recv(channel, p, opt | IKC_NO_NOTIFY)) != 0) {
		ihk_ikc_free(p);
		goto out;
	}

	/*
	 * XXX: Handler must release the packet eventually using
	 * ihk_ikc_release_packet().
	 *
	 * (syscall_packet_handler() is the function called for syscalls)
	 */
	h(channel, p, harg);

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
	list_for_each_entry(c, channels, list_all) {
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
IHK_EXPORT_SYMBOL(ihk_ikc_release_packet);

