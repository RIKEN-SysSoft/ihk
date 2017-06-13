/**
 * \file ikc/master.c
 * \brief IHK-IKC: Master channel handlers and connection managements
 *
 * Copyright (C) 2011-2012 Taku Shimosawa <shimosawa@is.s.u-tokyo.ac.jp>
 */
#include <ikc/ihk.h>
#include <ikc/master.h>

//#define DEBUG_PRINT_IKC

#ifdef DEBUG_PRINT_IKC
#define dkprintf kprintf
#else
#define dkprintf(...)
#endif

#ifdef IHK_OS_MANYCORE
static ihk_spinlock_t listener_lock;
static struct ihk_ikc_listen_param *listeners[IHK_IKC_MAX_PORT];

static ihk_spinlock_t *ihk_ikc_get_listener_lock(ihk_os_t os)
{
	return &listener_lock;
}
static struct ihk_ikc_listen_param **ihk_ikc_get_listener_entry(ihk_os_t os,
                                                                int port)
{
	return &listeners[port];
}
#else
ihk_spinlock_t *ihk_ikc_get_listener_lock(ihk_os_t os);
struct ihk_ikc_listen_param **ihk_ikc_get_listener_entry(ihk_os_t os,
                                                         int port);
#endif

struct ihk_ikc_channel_desc *ihk_os_get_master_channel(ihk_os_t __os);

int ihk_ikc_listen_port(ihk_os_t os, struct ihk_ikc_listen_param *param)
{
	struct ihk_ikc_listen_param **p;
	ihk_spinlock_t *lock;
	unsigned long flags;
	int port;

	if (!param) {
		return -EINVAL;
	}

	port = param->port;
	if (port < 0 || port >= IHK_IKC_MAX_PORT) {
		return -EINVAL;
	}

	lock = ihk_ikc_get_listener_lock(os);

	flags = ihk_ikc_spinlock_lock(lock);
	p = ihk_ikc_get_listener_entry(os, port);
	if (*p) {
		ihk_ikc_spinlock_unlock(lock, flags);
		return -EBUSY;
	}
	*p = param;
	ihk_ikc_spinlock_unlock(lock, flags);

	return 0;
}
IHK_EXPORT_SYMBOL(ihk_ikc_listen_port);

static int ihk_ikc_master_send(ihk_os_t os,
                               uint32_t msg, uint32_t ref,
                               uint64_t a1, uint64_t a2, uint64_t a3,
                               uint64_t a4, uint64_t a5)
{
	struct ihk_ikc_master_packet packet;
	struct ihk_ikc_channel_desc *c;

	c = ihk_ikc_get_master_channel(os);

	packet.msg = msg;
	packet.ref = ref;
	packet.param[0] = a1;
	packet.param[1] = a2;
	packet.param[2] = a3;
	packet.param[3] = a4;
	packet.param[4] = a5;

	return ihk_ikc_send(c, &packet, 0);
}

int ihk_ikc_accept(struct ihk_ikc_channel_desc *cm, 
                   struct ihk_ikc_listen_param *p,
                   unsigned long packet_size,
                   unsigned long *rq, unsigned long *sq,
                   struct ihk_ikc_channel_desc **newc,
                   unsigned long remote_channel_va,
                   int magic, int intr_cpu)
{
	struct ihk_ikc_channel_info ci;
	struct ihk_ikc_channel_desc *c;
	int r;

	if (!p || !p->handler) {
		return -ECONNREFUSED;
	}
	if (p->magic != magic ) {
		return -ECONNREFUSED;
	}
	if (packet_size != p->pkt_size) {
		return -ECONNABORTED;
	}
	c = ihk_ikc_create_channel(cm->remote_os, p->port, p->pkt_size,
	                           p->queue_size, rq, sq, 0);
	if (!c) {
		return -ENOMEM;
	}
	
	memset(&ci, 0, sizeof(ci));
	ci.channel = c;
	
	if (p->ikc_direction == IHK_IKC_DIRECTION_RECV) {
		ihk_ikc_channel_set_cpu(c, intr_cpu);
		ihk_ikc_set_regular_channel(cm->remote_os, c, intr_cpu);
	}

	if ((r = p->handler(&ci)) != 0) {
		ihk_ikc_free_channel(c);
		return r;
	}
	
	c->handler = ci.packet_handler;
	c->remote_channel_va = remote_channel_va;

	*newc = c;
	return 0;
}

static int ihk_ikc_master_reply_handler(ihk_os_t os,
                                        struct ihk_ikc_master_packet *packet);

int ihk_ikc_master_channel_packet_handler(struct ihk_ikc_channel_desc *c,
                                          void *__packet, void *os)
{
	struct ihk_ikc_master_packet *packet = __packet;
	struct ihk_ikc_listen_param **p;
	struct ihk_ikc_channel_desc *newc = NULL;
	ihk_spinlock_t *lock;
	unsigned long flags;
	unsigned long remote_channel_va = 0;
	int ret = 0;

	if (!c || !packet) {
		return -EINVAL;
	}

	switch (packet->msg) {
	case IHK_IKC_MASTER_MSG_PACKET_ON_CHANNEL:
	{
		struct ihk_ikc_channel_desc *c =
			(struct ihk_ikc_channel_desc *)packet->param[3];
		if (!c) {
			ret = -ENOENT;
			break;
		}
		if (os == NULL && c->recv.queue->read_cpu !=
				ihk_ikc_get_processor_id()) {
			kprintf("%s: %p is for CPU %d\n", __FUNCTION__,
					(void *)virt_to_phys(c), c->recv.queue->read_cpu);
		}
		if (ihk_ikc_channel_enabled(c) &&
				!ihk_ikc_queue_is_empty(c->recv.queue)) {
			ihk_ikc_recv_handler(c, c->handler, os, 0);
		}

		break;
	}
	case IHK_IKC_MASTER_MSG_CONNECT:
	{
		/* connect (port | packet size, recv queue, send queue) */
		unsigned long rq, sq;
		int port, r;

 		dkprintf("Connect msg: %x, %llx, %llx, %llx\n",
		        packet->ref, packet->param[0], packet->param[1],
		        packet->param[2]);

		port = (int)(packet->param[0] & 0xffffffffUL);
		if (port < 0 || port >= IHK_IKC_MAX_PORT) {
			r = EINVAL;
		} else {
			rq = packet->param[1];
			sq = packet->param[2];
			remote_channel_va = packet->param[3];

			lock = ihk_ikc_get_listener_lock(os);
			flags = ihk_ikc_spinlock_lock(lock);
			p = ihk_ikc_get_listener_entry(os, port);
			r = ihk_ikc_accept(c, *p,
			                   packet->param[0] >> 32,
			                   &rq, &sq, &newc,
			                   remote_channel_va, (int)packet->param[4],
			                   (int)(packet->param[4] >> 32));
			ihk_ikc_spinlock_unlock(lock, flags);
		}

		if (r != 0) {
			ihk_ikc_master_send(os,
			                    IHK_IKC_MASTER_MSG_CONNECT_REPLY,
			                    packet->ref, -r, 0, 0, 0, 0);
		} else {
			dkprintf("(Accepted) channel: %p, remote channel: %p\n",
			        newc, (void *)newc->remote_channel_va);
			newc->remote_channel_id = packet->ref;
			ihk_ikc_enable_channel(newc);
			ihk_ikc_master_send(os,
			                    IHK_IKC_MASTER_MSG_CONNECT_REPLY,
			                    packet->ref, 0, rq,
			                    remote_channel_va, (uint64_t)newc, 0);
		}

		break;
	}
	case IHK_IKC_MASTER_MSG_CONNECT_REPLY:
		ret = ihk_ikc_master_reply_handler(os, packet);
		break;

	case IHK_IKC_MASTER_MSG_DISCONNECT:
		newc = (struct ihk_ikc_channel_desc *)packet->param[3];
		dkprintf("disconnect channel #%d => %p\n", packet->ref, newc);
		if (!newc) {
			ret = -ENOENT;
			break;
		}

		flags = ihk_ikc_spinlock_lock(&newc->recv.lock);
		newc->flag |= IKC_FLAG_DESTROY_ACKED;
		ihk_ikc_spinlock_unlock(&newc->recv.lock, flags);

		if (!(newc->flag & IKC_FLAG_DESTROYING)) {
			/* It will not sleep 
			 * because it's already marked acked */
			dkprintf("Disconnect ack: %x\n",
			        newc->remote_channel_id);
			ihk_ikc_disconnect(newc);
		}

		ret = ihk_ikc_master_reply_handler(os, packet);
		break;

	default:
		ret = call_arch_master_packet_handler(os, c, __packet);
		break;
	}

	ihk_ikc_release_packet(__packet, c);

	return ret;
}

struct list_head *ihk_ikc_get_master_wait_list(ihk_os_t os);
ihk_spinlock_t *ihk_ikc_get_master_wait_lock(ihk_os_t os);

int ihk_ikc_wait_master(struct ihk_ikc_master_wait_struct *wq);

void ihk_ikc_wait_reply_prepare(ihk_os_t os, 
                                struct ihk_ikc_master_wait_struct *ws,
                                uint32_t msg, uint32_t ref)
{
	struct list_head *list;
	ihk_spinlock_t *lock;
	unsigned long flags;

	INIT_LIST_HEAD(&ws->list);
	ws->msg = msg;
	ws->ref = ref;
	ws->status = 0;
	memset(&ws->res, 0, sizeof(ws->res));

	ihk_ikc_wait_init(&ws->wait);

	list = ihk_ikc_get_master_wait_list(os);
	lock = ihk_ikc_get_master_wait_lock(os);

	flags = ihk_ikc_spinlock_lock(lock);
	list_add_tail(&ws->list, list);
	ihk_ikc_spinlock_unlock(lock, flags);
}

void ihk_ikc_wait_finish(ihk_os_t os, struct ihk_ikc_master_wait_struct *ws)
{
	ihk_spinlock_t *lock;
	unsigned long flags;

	lock = ihk_ikc_get_master_wait_lock(os);

	flags = ihk_ikc_spinlock_lock(lock);
	list_del(&ws->list);
	ihk_ikc_spinlock_unlock(lock, flags);
}

int ihk_ikc_master_reply_handler(ihk_os_t os,
                                 struct ihk_ikc_master_packet *packet)
{
	struct ihk_ikc_master_wait_struct *wq, *next;
	struct list_head *list;
	ihk_spinlock_t *lock;
	unsigned long flags;

	list = ihk_ikc_get_master_wait_list(os);
	lock = ihk_ikc_get_master_wait_lock(os);

	flags = ihk_ikc_spinlock_lock(lock);
	list_for_each_entry_safe(wq, next, list, list) {
		if (wq->msg == packet->msg && wq->ref == packet->ref) {
			memcpy(&wq->res, packet, sizeof(*packet));

			wq->status = 1;

			ihk_ikc_wake_master(wq);
		}
	}
	ihk_ikc_spinlock_unlock(lock, flags);

	return 0;
}

/* sync version. may sleep */
int ihk_ikc_connect(ihk_os_t os, struct ihk_ikc_connect_param *p)
{
	struct ihk_ikc_channel_desc *c;
	unsigned long rq = 0, sq = 0;
	int ref, ret;
	struct ihk_ikc_master_wait_struct wq;

	if (!p) {
		return -EINVAL;
	}

	dkprintf("%s: connecting channel %p\n", __FUNCTION__, c);
	c = ihk_ikc_create_channel(os, p->port, p->pkt_size, p->queue_size,
	                           &rq, &sq, 0);
	if (!c) {
		return -ENOMEM;
	}
	ref = c->channel_id;

	ihk_ikc_wait_reply_prepare(os, &wq, IHK_IKC_MASTER_MSG_CONNECT_REPLY,
	                           ref);

	if (ihk_ikc_master_send(os, IHK_IKC_MASTER_MSG_CONNECT, ref,
	                        ((unsigned long)p->pkt_size << 32) | p->port,
	                        sq, rq, (uint64_t)c,
	                        ((unsigned long)p->intr_cpu << 32) | p->magic) == 0) {
		ret = ihk_ikc_wait_master(&wq);
		ihk_ikc_wait_finish(os, &wq);

		if (ret != 0) {
			ihk_ikc_free_channel(c);
			return -EINTR;
		} else if (wq.res.param[0]){
			ihk_ikc_free_channel(c);
			return -wq.res.param[0];
		} else {
			dkprintf("response = %llx, %llx, %llx\n",
			        wq.res.param[0], wq.res.param[1],
			        wq.res.param[2]);
			ihk_ikc_set_remote_queue(&c->send, os, wq.res.param[1],
			                         p->queue_size);
			c->remote_channel_id = c->send.cache.channel_id;
			c->remote_channel_va = wq.res.param[3];
			dkprintf("%s: IHK_IKC_MASTER_MSG_CONNECT_REPLY"
					" channel: %p, remote_channel_va: %p\n",
					__FUNCTION__, c, c->remote_channel_va);
			c->handler = p->handler;
			c->send.queue->write_cpu = c->recv.queue->read_cpu;
			c->send.intr_cpu = p->intr_cpu;
			dkprintf("(Connected) Remote channeld id = %x\n",
			        c->remote_channel_id);
			ihk_ikc_enable_channel(c);
		}
	} else {
		ihk_ikc_wait_finish(os, &wq);
		ihk_ikc_free_channel(c);
		return -EBUSY;
	}

	p->channel = c;
	return 0;
}
IHK_EXPORT_SYMBOL(ihk_ikc_connect);


int __ihk_send_disconnect(struct ihk_ikc_channel_desc *c)
{
	return ihk_ikc_master_send(c->remote_os, IHK_IKC_MASTER_MSG_DISCONNECT, 
	                           c->remote_channel_id,
	                           c->channel_id, 0, 0, c->remote_channel_va, 0);
}

int __ihk_wait_for_disconnect_ack(struct ihk_ikc_channel_desc *c)
{
	struct ihk_ikc_master_wait_struct wq;
	int ret;
	ihk_os_t os = c->remote_os;

	ihk_ikc_wait_reply_prepare(os, &wq, 
	                           IHK_IKC_MASTER_MSG_DISCONNECT,
	                           c->channel_id);

	if (__ihk_send_disconnect(c) != 0) {
		ihk_ikc_wait_finish(os, &wq);
		return -EBUSY;
	}

	if (!(c->flag & IKC_FLAG_DESTROY_ACKED)) {
		ret = ihk_ikc_wait_master(&wq);
	} else {
		ret = 0;
	}
	ihk_ikc_wait_finish(os, &wq);
	
	return ret;
}

/* sync version. may sleep */
int ihk_ikc_disconnect(struct ihk_ikc_channel_desc *c)
{
	unsigned long flags, cflag;
	int r = 0;

	if (!c) {
		return -EINVAL;
	}

	flags = ihk_ikc_spinlock_lock(&c->lock);
	c->flag &= ~IKC_FLAG_ENABLED;
	if (c->flag & IKC_FLAG_DESTROYING) {
		ihk_ikc_spinlock_unlock(&c->lock, flags);
		return -EBUSY;
	}
	c->flag |= IKC_FLAG_DESTROYING;
	cflag = c->flag;
	ihk_ikc_spinlock_unlock(&c->lock, flags);
	
	if (!(cflag & IKC_FLAG_DESTROY_ACKED)) {
		r = __ihk_wait_for_disconnect_ack(c);
	} else {
		r = __ihk_send_disconnect(c);
	}
	
	return r;
}
IHK_EXPORT_SYMBOL(ihk_ikc_disconnect);

void ihk_ikc_destroy_channel(struct ihk_ikc_channel_desc *c)
{
    if (!c) {
        return;
    }
    ihk_ikc_disable_channel(c);
    ihk_ikc_free_channel(c);
}
IHK_EXPORT_SYMBOL(ihk_ikc_destroy_channel);
