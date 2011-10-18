#include <ikc/aal.h>
#include <ikc/queue.h>
#include <ikc/master.h>

struct aal_ikc_channel_desc *aal_mc_get_master_channel(void);

static LIST_HEAD(aal_ikc_channels);

struct list_head *aal_ikc_get_channel_list(aal_os_t os)
{
	return &aal_ikc_channels;
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
struct aal_ikc_channel_desc *aal_ikc_get_master_channel(aal_os_t os)
{
	return aal_mc_get_master_channel();
}

static struct aal_mc_interrupt_handler aal_ikc_handler = {
	.func = aal_ikc_interrupt_handler,
	.priv = NULL,
};

void aal_ikc_system_init(aal_os_t os)
{
	INIT_LIST_HEAD(&aal_ikc_handler.list);
	aal_mc_register_interrupt_handler(aal_mc_get_vector(AAL_GV_IKC),
	                                  &aal_ikc_handler);
}

void aal_ikc_system_exit(aal_os_t os)
{
	aal_mc_unregister_interrupt_handler(aal_mc_get_vector(AAL_GV_IKC),
	                                    &aal_ikc_handler);
}

struct aal_ikc_queue_head *aal_ikc_alloc_queue(int qpages)
{
	return aal_mc_alloc_pages(qpages, 0);
}

void aal_ikc_free_queue(struct aal_ikc_queue_head *q)
{
	aal_mc_free_pages(q, (sizeof(struct aal_ikc_queue_head) + 
	                      q->queue_size + PAGE_SIZE - 1) >> PAGE_SHIFT);
}

void *aal_ikc_malloc(int size)
{
	/* XXX: malloc is not implemented. Get one page even for a byte! */
	return aal_mc_alloc_pages(1, 0);
}
void aal_ikc_free(void *p)
{
	aal_mc_free_pages(p, 1);
}

extern aal_ikc_ph_t arch_master_channel_packet_handler;

int call_arch_master_packet_handler(void *os, struct aal_ikc_channel_desc *c,
                                    void *__packet)
{
	return arch_master_channel_packet_handler(c, __packet, os);
}

static struct list_head wait_list;
static aal_spinlock_t wait_lock;

struct list_head *aal_ikc_get_master_wait_list(aal_os_t aal_os)
{
	return &wait_list;
}

aal_spinlock_t *aal_ikc_get_master_wait_lock(aal_os_t aal_os)
{
	return &wait_lock;
}

void aal_ikc_wait_init(aal_wait_t *wait)
{
}

int aal_ikc_wait_master(struct aal_ikc_master_wait_struct *ws)
{
	/* XXX: SPINNING! */
	while (!ws->status) {
		cpu_pause();
		barrier();
	}
	return 0;
}

void aal_ikc_wake_master(struct aal_ikc_master_wait_struct *ws)
{
	ws->status = 1;
}
