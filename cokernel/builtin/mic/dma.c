#include <ihk/mm.h>
#include <ihk/lock.h>
#include <ihk/dma.h>
#include <errno.h>
#include "builtin_dma.h"

static struct builtin_dma_config_struct *builtin_mc_dma_config;

static inline unsigned long __next(struct builtin_dma_channel *c, unsigned long t)
{
	t++;
	if (t >= c->len) {
		t = 0;
	}
	return t;
}

static char __builtin_desc_check_room(struct builtin_dma_channel *c, int ndesc)
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

static struct builtin_dma_desc *desc_ptrs[BUILTIN_DMA_CHANNELS];

void builtin_mc_dma_init(unsigned long cfg_addr)
{
	int i;

	builtin_mc_dma_config = 
		map_fixed_area(cfg_addr, sizeof(struct builtin_dma_config_struct),
		               0);

	kprintf("DMA Config: %lx", cfg_addr);
	for (i = 0; i < BUILTIN_DMA_CHANNELS; i++) {
		desc_ptrs[i] =
			map_fixed_area(builtin_mc_dma_config->channels[i].desc_ptr,
			               PAGE_SIZE, 0);
		kprintf(" (%lx)", builtin_mc_dma_config->channels[i].desc_ptr);
	}
	kprintf("\n");
}

int ihk_mc_dma_request(int channel, struct ihk_dma_request *req)
{
	unsigned long flags;
	int ndesc = 1;
	struct builtin_dma_desc *desc, *desc_head;
	unsigned long h;
	struct builtin_dma_channel *c;

	c = &builtin_mc_dma_config->channels[1];

	if (req->callback || req->notify) {
		ndesc++;
	}

	flags = ihk_mc_spinlock_lock(&c->lock);

	if (!__builtin_desc_check_room(c, ndesc)) {
		ihk_mc_spinlock_unlock(&c->lock, flags);
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
			desc->param1 = ihk_mc_get_hardware_processor_id() |
				BUILTIN_DMA_DESC_PARAM1_INTR;
		} else if(req->notify) {
			desc->param2 = (void *)req->notify;
			desc->param4 = (unsigned long)req->priv;
		}
		h = __next(c, h);
	}

	c->head = h;
	ihk_mc_spinlock_unlock(&c->lock, flags);

	builtin_mc_dma_config->doorbell = 1;
	/*	builtin_dma_issue_interrupt(); */

	return 0;
}


