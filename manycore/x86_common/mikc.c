#include <aal/ikc.h>
#include <aal/lock.h>
#include <memory.h>
#include <string.h>

extern void arch_set_mikc_queue(void *r, void *w);

int aal_mc_ikc_init_first_local(struct aal_ikc_channel_desc *channel,
                                int (*packet_handler)(void *, void *))
{
	struct aal_ikc_queue_head *rq, *wq;
	struct aal_ikc_master_packet packet;

	aal_ikc_system_init();

	memset(channel, 0, sizeof(struct aal_ikc_channel_desc));

	/* Place both sides in this side */
	rq = arch_alloc_page(0);
	wq = arch_alloc_page(0);

	aal_ikc_init_queue(rq, 0, 0, PAGE_SIZE, MASTER_IKCQ_PKTSIZE);
	aal_ikc_init_queue(wq, 0, 0, PAGE_SIZE, MASTER_IKCQ_PKTSIZE);

	aal_ikc_init_desc(channel, IKC_DEST_HOST, 0, rq, wq, packet_handler);
	aal_ikc_enable_channel(channel);

	/* Set boot parameter */
	arch_set_mikc_queue(rq, wq);

	return 0;
}
