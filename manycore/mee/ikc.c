#include <aal/ikc.h>
#include <aal/lock.h>
#include <memory.h>
#include <string.h>

int aal_mc_ikc_init_first(struct aal_ikc_channel_desc *channel,
                          int (*packet_handler)(void *))
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

	aal_ikc_init_desc(channel, IKC_DEST_HOST, 0, rq, wq);
	aal_ikc_enable_channel(channel);

	packet.msg = MASTER_PACKET_NOP;
	aal_ikc_send(channel, &packet, 0);

	return 0;
}


