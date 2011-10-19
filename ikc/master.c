#include <ikc/aal.h>
#include <ikc/master.h>

#ifdef AAL_OS_MANYCORE
static aal_spinlock_t listener_lock;
static struct aal_ikc_listen_param *listeners[AAL_IKC_MAX_PORT];

static aal_spinlock_t *aal_ikc_get_listener_lock(aal_os_t os)
{
	return &listener_lock;
}
static struct aal_ikc_listen_param **aal_ikc_get_listener_entry(aal_os_t os,
                                                                int port)
{
	return &listeners[port];
}
#else
aal_spinlock_t *aal_ikc_get_listener_lock(aal_os_t os);
struct aal_ikc_listen_param **aal_ikc_get_listener_entry(aal_os_t os,
                                                         int port);
#endif

struct aal_ikc_channel_desc *aal_os_get_master_channel(aal_os_t __os);

int aal_ikc_listen_port(aal_os_t os, struct aal_ikc_listen_param *param)
{
	struct aal_ikc_listen_param **p;
	aal_spinlock_t *lock;
	unsigned long flags;
	int port = param->port;

	if (port < 0 || port >= AAL_IKC_MAX_PORT) {
		return -EINVAL;
	}

	lock = aal_ikc_get_listener_lock(os);

	flags = aal_ikc_spinlock_lock(lock);
	p = aal_ikc_get_listener_entry(os, port);
	if (*p) {
		aal_ikc_spinlock_unlock(lock, flags);
		return -EBUSY;
	}
	*p = param;
	aal_ikc_spinlock_unlock(lock, flags);

	return 0;
}
AAL_EXPORT_SYMBOL(aal_ikc_listen_port);

static int aal_ikc_master_send(aal_os_t os,
                               uint32_t msg, uint32_t ref,
                               uint64_t a1, uint64_t a2, uint64_t a3)
{
	struct aal_ikc_master_packet packet;
	struct aal_ikc_channel_desc *c;

	c = aal_ikc_get_master_channel(os);

	packet.msg = msg;
	packet.ref = ref;
	packet.param[0] = a1;
	packet.param[1] = a2;
	packet.param[2] = a3;

	kprintf("master_channel.send : %p\n", c->send.queue);
	return aal_ikc_send(c, &packet, 0);
}

int aal_ikc_accept(struct aal_ikc_channel_desc *cm, 
                   struct aal_ikc_listen_param *p,
                   unsigned long packet_size,
                   unsigned long *rq, unsigned long *sq,
                   struct aal_ikc_channel_desc **newc)
{
	struct aal_ikc_channel_info ci;
	struct aal_ikc_channel_desc *c;
	int r;

	if (!p || !p->handler) {
		return -ECONNREFUSED;
	}
	if (packet_size != p->pkt_size) {
		return -ECONNABORTED;
	}
	c = aal_ikc_create_channel(cm->remote_os, p->port, p->pkt_size,
	                           p->queue_size, rq, sq);
	if (!c) {
		return -ENOMEM;
	}
	
	memset(&ci, 0, sizeof(ci));
	ci.channel = c;
	if ((r = p->handler(&ci)) != 0) {
		aal_ikc_free_channel(c);
		return r;
	}
	
	c->handler = ci.packet_handler;

	*newc = c;
	return 0;
}

static int aal_ikc_master_reply_handler(aal_os_t os,
                                        struct aal_ikc_master_packet *packet);

int aal_ikc_master_channel_packet_handler(struct aal_ikc_channel_desc *c,
                                          void *__packet, void *os)
{
	struct aal_ikc_master_packet *packet = __packet;
	struct aal_ikc_listen_param **p;
	struct aal_ikc_channel_desc *newc = NULL;
	aal_spinlock_t *lock;
	unsigned long flags;

	switch (packet->msg) {
	case AAL_IKC_MASTER_MSG_CONNECT:
	{
		/* connect (port | packet size, recv queue, send queue) */
		unsigned long rq, sq;
		int port, r;

 		kprintf("Connect msg: %x, %llx, %llx, %llx\n",
		        packet->ref, packet->param[0], packet->param[1],
		        packet->param[2]);

		port = (int)(packet->param[0] & 0xffffffffUL);
		if (port < 0 || port >= AAL_IKC_MAX_PORT) {
			r = EINVAL;
		} else {
			rq = packet->param[1];
			sq = packet->param[2];

			lock = aal_ikc_get_listener_lock(os);
			flags = aal_ikc_spinlock_lock(lock);
			p = aal_ikc_get_listener_entry(os, port);
			r = aal_ikc_accept(c, *p,
			                   packet->param[0] >> 32,
			                   &rq, &sq, &newc);
			aal_ikc_spinlock_unlock(lock, flags);
		}

		if (r != 0) {
			aal_ikc_master_send(os,
			                    AAL_IKC_MASTER_MSG_CONNECT_REPLY,
			                    packet->ref, -r, 0, 0);
		} else {
			aal_ikc_enable_channel(newc);
			aal_ikc_master_send(os,
			                    AAL_IKC_MASTER_MSG_CONNECT_REPLY,
			                    packet->ref, 0, rq, 0);
		}

		break;
	}
	case AAL_IKC_MASTER_MSG_CONNECT_REPLY:
		return aal_ikc_master_reply_handler(os, packet);

	default:
		return call_arch_master_packet_handler(os, c, __packet);
	}

	return 0;
}

static aal_atomic_t connect_refnum;

struct list_head *aal_ikc_get_master_wait_list(aal_os_t os);
aal_spinlock_t *aal_ikc_get_master_wait_lock(aal_os_t os);

int aal_ikc_wait_master(struct aal_ikc_master_wait_struct *wq);

void aal_ikc_wait_reply_prepare(aal_os_t os, 
                                struct aal_ikc_master_wait_struct *ws,
                                uint32_t msg, uint32_t ref)
{
	struct list_head *list;
	aal_spinlock_t *lock;
	unsigned long flags;

	INIT_LIST_HEAD(&ws->list);
	ws->msg = msg;
	ws->ref = ref;
	ws->status = 0;
	memset(&ws->res, 0, sizeof(ws->res));

	aal_ikc_wait_init(&ws->wait);

	list = aal_ikc_get_master_wait_list(os);
	lock = aal_ikc_get_master_wait_lock(os);

	flags = aal_ikc_spinlock_lock(lock);
	list_add_tail(&ws->list, list);
	aal_ikc_spinlock_unlock(lock, flags);
}

int aal_ikc_master_reply_handler(aal_os_t os,
                                 struct aal_ikc_master_packet *packet)
{
	struct aal_ikc_master_wait_struct *wq, *next;
	struct list_head *list;
	aal_spinlock_t *lock;
	unsigned long flags;

	list = aal_ikc_get_master_wait_list(os);
	lock = aal_ikc_get_master_wait_lock(os);

	flags = aal_ikc_spinlock_lock(lock);
	list_for_each_entry_safe(wq, next, list, list) {
		if (wq->msg == packet->msg && wq->ref == packet->ref) {
			memcpy(&wq->res, packet, sizeof(*packet));
			list_del(&wq->list);

			wq->status = 1;

			aal_ikc_wake_master(wq);
		}
	}
	aal_ikc_spinlock_unlock(lock, flags);

	return 0;
}

/* sync version. may sleep */
int aal_ikc_connect(aal_os_t os, struct aal_ikc_connect_param *p)
{
	struct aal_ikc_channel_desc *c;
	unsigned long rq = 0, sq = 0;
	int ref;
	struct aal_ikc_master_wait_struct wq;

	c = aal_ikc_create_channel(os, p->port, p->pkt_size, p->queue_size,
	                           &rq, &sq);
	if (!c) {
		return -ENOMEM;
	}
	ref = aal_atomic_inc_return(&connect_refnum);

	aal_ikc_wait_reply_prepare(os, &wq, AAL_IKC_MASTER_MSG_CONNECT_REPLY,
	                           ref);

	kprintf("os = %p\n", os);

	if (aal_ikc_master_send(os, AAL_IKC_MASTER_MSG_CONNECT, ref,
	                        ((unsigned long)p->pkt_size << 32) 
	                        | p->port, sq, rq) == 0) {
		if (aal_ikc_wait_master(&wq) != 0) {
			aal_ikc_free_channel(c);
			return -EINTR;
		} else if (wq.res.param[0]){
			aal_ikc_free_channel(c);
			return -wq.res.param[0];
		} else {
			kprintf("response = %llx, %llx, %llx\n",
			        wq.res.param[0], wq.res.param[1],
			        wq.res.param[2]);
			aal_ikc_set_remote_queue(&c->send, os, wq.res.param[1],
			                         p->queue_size);
			c->handler = p->handler;
			aal_ikc_enable_channel(c);
		}
	} else {
		aal_ikc_free_channel(c);
		return -EBUSY;
	}

	p->channel = c;
	return 0;
}
AAL_EXPORT_SYMBOL(aal_ikc_connect);
