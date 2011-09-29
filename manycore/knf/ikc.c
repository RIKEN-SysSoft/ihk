#include <aal/ikc.h>
#include <memory.h>
#include <string.h>

static void aal_mc_ikc_init_desc(struct aal_mc_ikc_channel_desc *c,
                                 int rid, int cid,
                                 struct aal_ikc_queue_head *rq,
                                 struct aal_ikc_queue_head *wq)
{
	c->remote_id = rid;
	c->channel_id = cid;
	c->recv.queue = rq;
	c->send.queue = wq;
}


int aal_mc_ikc_init_first(struct aal_mc_ikc_channel_desc *channel)
{
	struct aal_ikc_queue_head *rq, *wq;

	memset(channel, 0, sizeof(struct aal_mc_ikc_channel_desc));

	/* Place both sides in this side */
	rq = arch_alloc_page(0);
	wq = arch_alloc_page(0);

	aal_ikc_init_queue(rq, 0, 0, PAGE_SIZE, MASTER_IKCQ_PKTSIZE);
	aal_ikc_init_queue(wq, 0, 0, PAGE_SIZE, MASTER_IKCQ_PKTSIZE);

	aal_mc_ikc_init_desc(channel, IKC_DEST_HOST, 0, rq, wq);
}
