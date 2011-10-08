#include <ikc/aal.h>
#include <ikc/queue.h>

void aal_ikc_notify_remote_read(struct aal_ikc_channel_desc *c);
void aal_ikc_notify_remote_write(struct aal_ikc_channel_desc *c);
void aal_ikc_send_interrupt(struct aal_ikc_channel_desc *c);

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

	kprintf("initq: %p, %d, %d\n", q, q->pktsize, q->pktcount);

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
		memcpy(packet, q + sizeof(*q) + q->read_off, q->pktsize);

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

		h(c, q + sizeof(*q) + q->read_off, harg);

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
		memcpy(q + sizeof(*q) + q->write_off, packet, q->pktsize);

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
                       int rid, int cid,
                       struct aal_ikc_queue_head *rq,
                       struct aal_ikc_queue_head *wq,
                       aal_ikc_ph_t packet_handler)
{
	c->remote_id = rid;
	c->channel_id = cid;
	c->recv.queue = rq;
	c->send.queue = wq;
	c->handler = packet_handler;

	aal_ikc_spinlock_init(&c->recv.lock);
	aal_ikc_spinlock_init(&c->send.lock);
}

int aal_ikc_send(struct aal_ikc_channel_desc *channel, void *p, int opt)
{
	unsigned long flags;
	int r;

	flags = aal_ikc_spinlock_lock(&channel->send.lock);
	r = aal_ikc_write_queue(channel->send.queue, p, opt);
	aal_ikc_notify_remote_write(channel);
	aal_ikc_spinlock_unlock(&channel->send.lock, flags);

	return r;
}

int aal_ikc_recv(struct aal_ikc_channel_desc *channel, void *p, int opt)
{
	unsigned long flags;
	int r;

	flags = aal_ikc_spinlock_lock(&channel->recv.lock);
	r = aal_ikc_read_queue(channel->recv.queue, p, opt);
	/* XXX: Optimal interrupt */
	aal_ikc_notify_remote_read(channel);
	aal_ikc_spinlock_unlock(&channel->recv.lock, flags);

	return r;
}

int aal_ikc_recv_handler(struct aal_ikc_channel_desc *channel, 
                         aal_ikc_ph_t h, void *harg, int opt)
{
	unsigned long flags;
	int r;

	flags = aal_ikc_spinlock_lock(&channel->recv.lock);
	r = aal_ikc_read_queue_handler(channel->recv.queue, channel,
	                               h, harg, opt);
	/* XXX: Optimal interrupt */
	aal_ikc_notify_remote_read(channel);
	aal_ikc_spinlock_unlock(&channel->recv.lock, flags);

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

#ifdef AAL_OS_MANYCORE
static LIST_HEAD(aal_ikc_channels);

void aal_ikc_enable_channel(struct aal_ikc_channel_desc *channel)
{
	list_add_tail(&channel->list, &aal_ikc_channels);
}

void aal_ikc_disable_channel(struct aal_ikc_channel_desc *channel)
{
	list_del(&channel->list);
}

static void aal_ikc_interrupt_handler(void *priv)
{
	/* This should be done in the software irq... */
	struct aal_ikc_channel_desc *c;

	/* XXX: Linear search? */
	list_for_each_entry(c, &aal_ikc_channels, list) {
		if (!aal_ikc_queue_is_empty(c->recv.queue)) {
			aal_ikc_recv_handler(c, c->handler, NULL, 0);
		}
	}
}

static struct aal_mc_interrupt_handler aal_ikc_handler = {
	.func = aal_ikc_interrupt_handler,
	.priv = NULL,
};

void aal_ikc_system_init(void)
{
	INIT_LIST_HEAD(&aal_ikc_handler.list);
	aal_mc_register_interrupt_handler(aal_mc_get_vector(AAL_GV_IKC),
	                                  &aal_ikc_handler);
}

void aal_ikc_system_exit(void)
{
	aal_mc_unregister_interrupt_handler(aal_mc_get_vector(AAL_GV_IKC),
	                                    &aal_ikc_handler);
}
#else

extern struct aal_host_interrupt_handler *aal_host_os_get_ikc_handler(aal_os_t);
extern struct list_head *aal_host_os_get_ikc_channel_list(aal_os_t);

void aal_ikc_enable_channel(struct aal_ikc_channel_desc *channel)
{
	struct list_head *channels;

	channels = aal_host_os_get_ikc_channel_list(channel->remote_os);
	list_add_tail(&channel->list, channels);
}

void aal_ikc_disable_channel(struct aal_ikc_channel_desc *channel)
{
	list_del(&channel->list);
}

static void aal_ikc_interrupt_handler(aal_os_t os, void *os_priv, void *priv)
{
	/* This should be done in the software irq... */
	struct aal_ikc_channel_desc *c;
	struct list_head *channels;

	channels = aal_host_os_get_ikc_channel_list(priv);

	/* XXX: Linear search? */
	list_for_each_entry(c, channels, list) {
		if (!aal_ikc_queue_is_empty(c->recv.queue)) {
			aal_ikc_recv_handler(c, c->handler, os, 0);
		}
	}
}

void aal_ikc_system_init(aal_os_t os)
{
	struct aal_host_interrupt_handler *h;
	
	h = aal_host_os_get_ikc_handler(os);
	
	INIT_LIST_HEAD(&h->list);
	h->func = aal_ikc_interrupt_handler;
	h->priv = os;

	aal_os_register_interrupt_handler(os, 0, h);
}

void aal_ikc_system_exit(aal_os_t os)
{
	struct aal_host_interrupt_handler *h;
	
	h = aal_host_os_get_ikc_handler(os);
	
	aal_os_unregister_interrupt_handler(os, 0, h);
}
#endif
