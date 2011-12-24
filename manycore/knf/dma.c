#include <aal/debug.h>
#include <aal/mm.h>
#include <aal/cpu.h>
#include <aal/lock.h>
#include <aal/dma.h>
#include <types.h>
#include <errno.h>
#include <string.h>
#include "knf.h"

struct knf_dma_channel {
	aal_spinlock_t lock;
	int head, tail;
	int channel;
	int desc_count;
	int owner;
	union md_mic_dma_desc *desc;
};

static unsigned int sbox_dma_read(struct knf_dma_channel *c, int index)
{
	return sbox_read(index + 0x40 * c->channel);
}

static void sbox_dma_write(struct knf_dma_channel *c,
                           int index, unsigned int value)
{
	sbox_write(index + 0x40 * c->channel, value);
}

void aal_mc_dma_enable_channel(int channel)
{
	unsigned int dcr;

	dcr = sbox_read(SBOX_DCR);
	dcr |= (2 << channel * 2); 
	sbox_write(SBOX_DCR, dcr);
}

void aal_mc_dma_disable_channel(int channel)
{
	unsigned int dcr;

	dcr = sbox_read(SBOX_DCR);
	dcr &= ~(2 << (channel * 2));
	sbox_write(SBOX_DCR, dcr);
}

struct knf_dma_channel channels[8];

static void __debug_print_dma_reg(struct knf_dma_channel *c)
{
	kprintf("Channel %d:\n", c->channel);
	kprintf("DCR : %x\n", sbox_read(SBOX_DCR));
	kprintf("DRAR-HI : %x, LO : %x\n", sbox_dma_read(c, SBOX_DRAR_HI_0),
	        sbox_dma_read(c, SBOX_DRAR_LO_0));
	kprintf("DTPR : %x, DHPR : %x\n", sbox_dma_read(c, SBOX_DTPR_0),
	        sbox_dma_read(c, SBOX_DHPR_0));
}

static void __initialize_dma(struct knf_dma_channel *c)
{
	/* MIC_OWNED = 0, HOST_OWNED = 1, ENABLED = 2 */
	unsigned long dcr, drarh, drarl;
	int channel = 0;

	channel = c->channel;

	dcr = sbox_read(SBOX_DCR);
	dcr &= ~(3 << (channel * 2));
	dcr |= c->owner << (channel * 2);
	sbox_write(SBOX_DCR, dcr);

	drarh = SET_SBOX_DRARHI_SIZE(c->desc_count);
	drarh |= SET_SBOX_DRARHI_BA((unsigned long)virt_to_phys(c->desc) >> 32);
	drarl = (unsigned long)virt_to_phys(c->desc) & 0xffffffff;

	sbox_dma_write(c, SBOX_DRAR_HI_0, drarh);
	sbox_dma_write(c, SBOX_DRAR_LO_0, drarl);
	
	sbox_dma_write(c, SBOX_DTPR_0, 0);
	sbox_dma_write(c, SBOX_DHPR_0, 0);

	c->head = c->tail = 0;
}

static void __initialize_host_dma(struct knf_dma_channel *c)
{
	/* MIC_OWNED = 0, HOST_OWNED = 1, ENABLED = 2 */
	unsigned long dcr;
	int channel = 0;

	channel = c->channel;

	dcr = sbox_read(SBOX_DCR);
	dcr &= ~(3 << (channel * 2));
	dcr |= c->owner << (channel * 2);
	sbox_write(SBOX_DCR, dcr);
}

void aal_mc_dma_init(void)
{
	/* Initialize only channels #0 and #4 */
	channels[0].channel = 0;
	channels[0].owner = 0;
	channels[0].desc = aal_mc_alloc_pages(1, 0);
	channels[0].desc_count = PAGE_SIZE / sizeof(union md_mic_dma_desc);
	__initialize_dma(channels + 0);
	aal_mc_dma_enable_channel(0);
	__debug_print_dma_reg(channels + 0);

	channels[4].channel = 4;
	channels[4].owner = 1;
	channels[4].desc = 0;
	channels[4].desc_count = 0;
	__initialize_host_dma(channels + 4);
	aal_mc_dma_enable_channel(4);
	__debug_print_dma_reg(channels + 4);
}


static char __knf_desc_check_room(struct knf_dma_channel *c, int ndesc)
{
	int h = c->head, t = c->tail, reg_value = 0;

	for (;;) {
		if (h <= t) {
			t += c->desc_count;
		}
		if (h + ndesc < t) { /* OK */
			return 1;
		}
		
		if (!reg_value) {
			t = c->tail = sbox_dma_read(c, SBOX_DTPR_0);
			reg_value = 1;
		} else {
			break;
		}
	}

	return 0; /* NG */
}

static union md_mic_dma_desc *__knf_desc_proceed_head(struct knf_dma_channel *c)
{
	union md_mic_dma_desc *d;

	d = c->desc + c->head;
	c->head++;
	if (c->head >= c->desc_count) {
		c->head = 0;
	}

	/* Clear the descriptor to return */
	d->qwords.qw0 = 0;
	d->qwords.qw1 = 0;

	return d;
}

/*
 * If callback is used (intr == 1)
 *   On completion, callback is called with priv as the argument
 * If non-callback is used (intr == 0)
 *   On completion, value is written to the priv
 */
int aal_mc_dma_request(int channel,
                       struct aal_dma_request *req)
{
	unsigned long flags;
	struct knf_dma_channel *c;
	int ndesc = 1;
	union md_mic_dma_desc *desc;

	c = &channels[channel];
	if (!c->desc) {
		return -EINVAL;
	}
	if (req->callback || req->notify) {
		ndesc++;
	}

	flags = aal_mc_spinlock_lock(&c->lock);
	if (!__knf_desc_check_room(c, ndesc)) {
		aal_mc_spinlock_unlock(&c->lock, flags);
		return -EBUSY;
	}
	
	desc = __knf_desc_proceed_head(c);
	desc->desc.memcpy.type = 1;
	desc->desc.memcpy.sap = req->src_phys;
	desc->desc.memcpy.dap = req->dest_phys;
	desc->desc.memcpy.length = req->size >> 6; /* 2^6 = 64 */

	kprintf("COPY : sap = %lx, dap = %lx, len = %lx\n",
	        desc->desc.memcpy.sap, desc->desc.memcpy.dap,
	        desc->desc.memcpy.length);
	if (ndesc > 1) {
		desc = __knf_desc_proceed_head(c);
		desc->desc.status.type = 2;
		if (req->callback) {
			desc->desc.status.intr = 1;
		} else {
			desc->desc.status.dap = (unsigned long)req->notify;
			desc->desc.status.data = req->priv;
		}
		kprintf("STATUS : dap = %lx, data = %lx, intr = %lx\n",
		        desc->desc.status.dap, desc->desc.status.data,
		        desc->desc.status.intr);
	}
	sbox_dma_write(c, SBOX_DHPR_0, c->head);
	
	aal_mc_spinlock_unlock(&c->lock, flags);

	__debug_print_dma_reg(c);

	return 0;
}
