#include <ikc/aal.h>
#include <ikc/master.h>
#include <linux/slab.h>
#include <asm/bitops.h>

extern struct list_head *aal_host_os_get_ikc_channel_list(aal_os_t aal_os);
struct aal_host_interrupt_handler *aal_host_os_get_ikc_handler(aal_os_t aal_os);
int aal_ikc_call_master_packet_handler(aal_os_t aal_os,
                                       struct aal_ikc_channel_desc *c,
                                       void *packet);
struct aal_ikc_channel_desc *aal_os_get_master_channel(aal_os_t __os);

struct list_head *aal_ikc_get_channel_list(aal_os_t os)
{
	return aal_host_os_get_ikc_channel_list(os);
}

static void aal_ikc_interrupt_handler(aal_os_t os, void *os_priv, void *priv)
{
	/* This should be done in the software irq... */
	struct aal_ikc_channel_desc *c;
	struct list_head *channels;

	channels = aal_ikc_get_channel_list(priv);

	/* XXX: Linear search? */
	list_for_each_entry(c, channels, list) {
		if (!aal_ikc_queue_is_empty(c->recv.queue)) {
			aal_ikc_recv_handler(c, c->handler, os, 0);
		}
	}
}

struct aal_ikc_channel_desc *aal_ikc_get_master_channel(aal_os_t os)
{
	return aal_os_get_master_channel(os);
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

struct aal_ikc_queue_head *aal_ikc_alloc_queue(int qpages)
{
	int order = fls(qpages) - 1;

	return (void *)__get_free_pages(GFP_KERNEL | GFP_ATOMIC, order);
}

void aal_ikc_free_queue(struct aal_ikc_queue_head *q)
{
	int qpages = (q->queue_size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	int order = fls(qpages) - 1;

	free_pages((unsigned long)q, order);
}

void *aal_ikc_malloc(int size)
{
	return kmalloc(size, GFP_KERNEL | GFP_ATOMIC);
}
void aal_ikc_free(void *p)
{
	kfree(p);
}

int call_arch_master_packet_handler(void *os, struct aal_ikc_channel_desc *c,
                                    void *__packet)
{
	return aal_ikc_call_master_packet_handler(os, c, __packet);
}

void aal_ikc_wait_init(aal_wait_t *wait)
{
	init_waitqueue_head(wait);
}

int aal_ikc_wait_master(struct aal_ikc_master_wait_struct *ws)
{
	return wait_event_interruptible(ws->wait, ws->status);
}

void aal_ikc_wake_master(struct aal_ikc_master_wait_struct *ws)
{
	wake_up_interruptible(&ws->wait);
}

