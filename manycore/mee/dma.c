#include <aal/mm.h>
#include <aal/lock.h>
#include <aal/dma.h>
#include <errno.h>
#include "mee_dma.h"

static struct mee_dma_config_struct *mee_mc_dma_config;

static inline unsigned long __next(struct mee_dma_channel *c, unsigned long t)
{
	t++;
	if (t >= c->len) {
		t = 0;
	}
	return t;
}

static char __mee_desc_check_room(struct mee_dma_channel *c, int ndesc)
{
	int h = c->head, t = c->tail;
 
	if (t <= h) {
		t += c->len;
	}
	if (h + ndesc < t) { /* OK */
		return 1;
	}

	return 0; /* NG */
}

static struct mee_dma_desc *desc_ptrs[MEE_DMA_CHANNELS];

void mee_mc_dma_init(unsigned long cfg_addr)
{
	int i;

	mee_mc_dma_config = 
		map_fixed_area(cfg_addr, sizeof(struct mee_dma_config_struct),
		               0);
	for (i = 0; i < MEE_DMA_CHANNELS; i++) {
		desc_ptrs[i] =
			map_fixed_area(mee_mc_dma_config->channels[i].desc_ptr,
			               PAGE_SIZE, 0);
	}
}

int aal_mc_dma_request(int channel, struct aal_dma_request *req)
{
	unsigned long flags;
	int ndesc = 1;
	struct mee_dma_desc *desc, *desc_head;
	unsigned long h;
	struct mee_dma_channel *c;

	c = &mee_mc_dma_config->channels[1];

	if (req->callback || req->notify) {
		ndesc++;
	}

	flags = aal_mc_spinlock_lock(&c->lock);

	if (!__mee_desc_check_room(c, ndesc)) {
		aal_mc_spinlock_unlock(&c->lock, flags);
		return -EBUSY;
	}

	h = c->head;

	desc_head = desc_ptrs[1];

	desc = desc_head + h;
	desc->type = 1;
	desc->param1 = 0;
	desc->param2 = (void *)req->src_phys;
	desc->param3 = (void *)req->dest_phys;
	desc->param4 = req->size;

	h = __next(c, h);

	if (ndesc > 1) {
		desc = desc_head + h;
		desc->type = 2;
		desc->param1 = 0;

		if (req->callback) {
			desc->param1 = aal_mc_get_hardware_processor_id() |
				MEE_DMA_DESC_PARAM1_INTR;
		} else if(req->notify) {
			desc->param2 = (void *)req->notify;
			desc->param4 = (unsigned long)req->priv;
		}
		h = __next(c, h);
	}

	c->head = h;
	aal_mc_spinlock_unlock(&c->lock, flags);

	mee_mc_dma_config->doorbell = 1;
	/*	mee_dma_issue_interrupt(); */

	return 0;
}


