#include <ikc/aal.h>
#include <ikc/queue.h>

void aal_ikc_notify_remote_read(struct aal_ikc_channel_desc *c);
void aal_ikc_notify_remote_write(struct aal_ikc_channel_desc *c);

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
int aal_ikc_init_queue(struct aal_ikc_queue_head *q,
                       int id, int type, int size, int packetsize)
{
	memset(q, 0, sizeof(*q));

	q->id = id;
	q->type = type;
	q->pktsize = packetsize;
	q->pktcount = (size - sizeof(struct aal_ikc_queue_head)) / packetsize;

	q->read_off = q->write_off = 0;
	q->read_cpu = 0;
	q->write_cpu = 0;
	q->queue_size = q->pktsize * q->pktcount;

	return 0;
}

int aal_ikc_queue_is_empty(struct aal_ikc_queue_head *q)
{
	return q->read_off == q->write_off;
}

int aal_ikc_queue_is_full(struct aal_ikc_queue_head *q)
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

int aal_ikc_read_queue(struct aal_ikc_queue_head *q, void *packet, int flag)
{
	uint64_t o;

	if (aal_ikc_queue_is_empty(q)) {
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

int aal_ikc_read_queue_handler(struct aal_ikc_queue_head *q, 
                               struct aal_ikc_channel_desc *c,
                               int (*h)(struct aal_ikc_channel_desc *,
                                        void *, void *), void *harg, int flag)
{
	uint64_t o;

	if (aal_ikc_queue_is_empty(q)) {
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

int aal_ikc_write_queue(struct aal_ikc_queue_head *q, void *packet, int flag)
{
	uint64_t o;

	if (aal_ikc_queue_is_full(q)) {
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
void aal_ikc_init_desc(struct aal_ikc_channel_desc *c,
                       aal_os_t ros, int port,
                       struct aal_ikc_queue_head *rq,
                       struct aal_ikc_queue_head *wq,
                       aal_ikc_ph_t packet_handler)
{
	struct list_head *channels = aal_ikc_get_channel_list(ros);

	INIT_LIST_HEAD(&c->list);
	INIT_LIST_HEAD(&c->all_list);

	c->remote_os = ros;
	c->port = port;
	c->channel_id = aal_ikc_get_unique_channel_id(ros);
	c->recv.queue = rq;
	c->send.queue = wq;
	if (rq) {
		c->recv.cache = *rq;
		c->recv.queue->channel_id = c->channel_id;
	}
	if (wq) {
		c->send.cache = *wq;
		c->remote_channel_id = c->send.cache.channel_id;
	}
	c->handler = packet_handler;

	aal_ikc_spinlock_init(&c->recv.lock);
	aal_ikc_spinlock_init(&c->send.lock);

	list_add_tail(&c->list, channels);
}

int aal_ikc_set_remote_queue(struct aal_ikc_queue_desc *q, aal_os_t os,
                             unsigned long rphys, unsigned long qsize)
{
	int qpages;

	qpages = (qsize + PAGE_SIZE - 1) >> PAGE_SHIFT;

	aal_ikc_spinlock_init(&q->lock);
	q->qrphys = rphys;
	q->qphys = aal_ikc_map_memory(os, q->qrphys, qpages * PAGE_SIZE);
	q->queue = aal_ikc_map_virtual(aal_os_to_dev(os), q->qphys,
	                               qpages * PAGE_SIZE,
	                               AAL_IKC_QUEUE_PT_ATTR);
	q->cache = *q->queue;

	return 0;
}

struct aal_ikc_channel_desc *aal_ikc_create_channel(aal_os_t os,
                                                    int port,
                                                    int packet_size,
                                                    unsigned long qsize,
                                                    unsigned long *rq,
                                                    unsigned long *sq,
                                                    enum aal_ikc_channel_flag f)
{
	unsigned long phys;
	struct aal_ikc_channel_desc *desc;
	struct aal_ikc_queue_head *recvq, *sendq;
	int qpages;

	qpages = (qsize + PAGE_SIZE - 1) >> PAGE_SHIFT;

	desc = aal_ikc_malloc(sizeof(struct aal_ikc_channel_desc)
	                      + packet_size);
	if (!desc) {
		return NULL;
	}

	memset(desc, 0, sizeof(*desc));

	desc->flag = f;

	if (!*rq) {
		recvq = aal_ikc_alloc_queue(qpages);
		if (!recvq) {
			return NULL;
		}

		aal_ikc_init_queue(recvq, 1, port, PAGE_SIZE * qpages,
		                   packet_size);
		*rq = virt_to_phys(recvq);

		desc->recv.qrphys = 0;
		desc->recv.qphys = *rq;
	} else {
		phys = aal_ikc_map_memory(os, *rq, qpages * PAGE_SIZE);
		recvq = aal_ikc_map_virtual(aal_os_to_dev(os), phys,
		                            qpages * PAGE_SIZE,
		                            AAL_IKC_QUEUE_PT_ATTR);

		desc->recv.qrphys = *rq;
		desc->recv.qphys = phys;
	}
	/* XXX: This do not assume local send queue */
	if (*sq) {
		phys = aal_ikc_map_memory(os, *sq, qpages * PAGE_SIZE);
		sendq = aal_ikc_map_virtual(aal_os_to_dev(os), phys,
		                            qpages,
		                            AAL_IKC_QUEUE_PT_ATTR);

		desc->send.qrphys = *sq;
		desc->send.qphys = phys;
	} else {
		sendq = NULL;
	}

	aal_ikc_init_desc(desc, os, port, recvq, sendq, NULL);

	return desc;
}

void aal_ikc_free_channel(struct aal_ikc_channel_desc *desc)
{
	aal_os_t os = desc->remote_os;
	int qpages;

	list_del(&desc->list);

	if (desc->recv.queue) {
		qpages = (desc->recv.queue->queue_size
		          + sizeof(struct aal_ikc_queue_head) + PAGE_SIZE - 1)
			>> PAGE_SHIFT;
		if (desc->recv.qrphys) {
			aal_ikc_unmap_virtual(aal_os_to_dev(os),
			                      desc->recv.queue,
			                      qpages * PAGE_SIZE);
			aal_ikc_unmap_memory(os, desc->recv.qphys, qpages);
		} else {
			aal_ikc_free_queue(desc->recv.queue);
		}
	}

	if (desc->send.queue) {
		qpages = (desc->send.queue->queue_size
		          + sizeof(struct aal_ikc_queue_head) + PAGE_SIZE - 1)
			>> PAGE_SHIFT;
		if (desc->send.qrphys) {
			aal_ikc_unmap_virtual(aal_os_to_dev(os),
			                      desc->send.queue,
			                      qpages * PAGE_SIZE);
			aal_ikc_unmap_memory(os, desc->send.qphys, qpages);
		} else {
			aal_ikc_free_queue(desc->send.queue);
		}
	}

	aal_ikc_free(desc);
}

int aal_ikc_send(struct aal_ikc_channel_desc *channel, void *p, int opt)
{
	unsigned long flags;
	int r;

	flags = aal_ikc_spinlock_lock(&channel->send.lock);
	if (aal_ikc_channel_enabled(channel)) {
		r = aal_ikc_write_queue(channel->send.queue, p, opt);
		if (!(opt & IKC_NO_NOTIFY)) {
			aal_ikc_notify_remote_write(channel);
		}
	} else {
		r = -EINVAL;
	}
	aal_ikc_spinlock_unlock(&channel->send.lock, flags);

	return r;
}

int aal_ikc_recv(struct aal_ikc_channel_desc *channel, void *p, int opt)
{
	unsigned long flags;
	int r;

	flags = aal_ikc_spinlock_lock(&channel->recv.lock);
	if (aal_ikc_channel_enabled(channel)) {
		r = aal_ikc_read_queue(channel->recv.queue, p, opt);
		/* XXX: Optimal interrupt */
		if (!(opt & IKC_NO_NOTIFY)) {
			aal_ikc_notify_remote_read(channel);
		}
	} else {
		r = -EINVAL;
	}
	aal_ikc_spinlock_unlock(&channel->recv.lock, flags);

	return r;
}

static int __aal_ikc_recv_nocopy(struct aal_ikc_channel_desc *channel,
                                 aal_ikc_ph_t h, void *harg, int opt)
{
	unsigned long flags;
	int r;

	flags = aal_ikc_spinlock_lock(&channel->recv.lock);
	if (aal_ikc_channel_enabled(channel) &&
	    !aal_ikc_queue_is_empty(channel->recv.queue)) {
		while (aal_ikc_read_queue_handler(channel->recv.queue,
		                                  channel,
		                                  h, harg, opt) == 0);
		/* XXX: Optimal interrupt */
		aal_ikc_notify_remote_read(channel);

		r = 0;
	} else {
		r = -EINVAL;
	}
	aal_ikc_spinlock_unlock(&channel->recv.lock, flags);

	return r;
}

int aal_ikc_recv_handler(struct aal_ikc_channel_desc *channel, 
                         aal_ikc_ph_t h, void *harg, int opt)
{
	int r = -ENOENT;

	if (channel->flag & IKC_FLAG_NO_COPY) {
		return __aal_ikc_recv_nocopy(channel, h, harg, opt);
	} else {
		while (aal_ikc_recv(channel, channel->packet_buf,
		                    opt | IKC_NO_NOTIFY) == 0) {
			h(channel, channel->packet_buf, harg);
			r = 0;
		}
		aal_ikc_notify_remote_read(channel);
	}
	return r;
}

void aal_ikc_notify_remote_read(struct aal_ikc_channel_desc *c)
{
	aal_ikc_send_interrupt(c);
}
void aal_ikc_notify_remote_write(struct aal_ikc_channel_desc *c)
{
	aal_ikc_send_interrupt(c);
}

void __aal_ikc_enable_channel(struct aal_ikc_channel_desc *channel)
{
	channel->flag |= IKC_FLAG_ENABLED;
}

void __aal_ikc_disable_channel(struct aal_ikc_channel_desc *channel)
{
	channel->flag &= ~IKC_FLAG_ENABLED;
}

void aal_ikc_enable_channel(struct aal_ikc_channel_desc *channel)
{
	unsigned long flags;

	flags = aal_ikc_spinlock_lock(&channel->recv.lock);
	__aal_ikc_enable_channel(channel);
	aal_ikc_spinlock_unlock(&channel->recv.lock, flags);
}

void aal_ikc_disable_channel(struct aal_ikc_channel_desc *channel)
{
	unsigned long flags;

	flags = aal_ikc_spinlock_lock(&channel->recv.lock);
	__aal_ikc_disable_channel(channel);
	aal_ikc_spinlock_unlock(&channel->recv.lock, flags);
}

struct aal_ikc_channel_desc *aal_ikc_find_channel(aal_os_t os, int id)
{
	aal_spinlock_t *lock = aal_ikc_get_channel_list_lock(os);
	struct list_head *channels = aal_ikc_get_channel_list(os);
	struct aal_ikc_channel_desc *c;
	unsigned long flags;

	flags = aal_ikc_spinlock_lock(lock);
	list_for_each_entry(c, channels, list) {
		if (c->channel_id == id) {
			aal_ikc_spinlock_unlock(lock, flags);
			return c;
		}
	}
	aal_ikc_spinlock_unlock(lock, flags);

	return NULL;
}

AAL_EXPORT_SYMBOL(aal_ikc_send);
AAL_EXPORT_SYMBOL(aal_ikc_recv);
AAL_EXPORT_SYMBOL(aal_ikc_recv_handler);
AAL_EXPORT_SYMBOL(aal_ikc_enable_channel);
AAL_EXPORT_SYMBOL(aal_ikc_disable_channel);
AAL_EXPORT_SYMBOL(aal_ikc_free_channel);
AAL_EXPORT_SYMBOL(aal_ikc_find_channel);
