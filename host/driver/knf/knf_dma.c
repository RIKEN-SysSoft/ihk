#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/interrupt.h>
#include <aal/misc/debug.h>

#include "mic.h"
#include "mic/mic_dma.h"
#include "knf_user.h"

static unsigned int sbox_dma_read(struct knf_dma_channel *c, int index)
{
	return knf_read_sbox(c->kdd, index + 0x40 * c->channel);
}

static void sbox_dma_write(struct knf_dma_channel *c,
                           int index, unsigned int value)
{
	knf_write_sbox(c->kdd, index + 0x40 * c->channel, value);
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

static unsigned long phys_to_mic_phys(unsigned long phys)
{
	/* This is true when phys < 32GB or so. */
	return phys + MIC_SYSTEM_BASE;
}

static unsigned long virt_to_mic_phys(void *virt)
{
	return phys_to_mic_phys(virt_to_phys(virt));
}

static void __initialize_dma(struct knf_dma_channel *c)
{
	/* MIC_OWNED = 0, HOST_OWNED = 1, ENABLED = 2 */
	unsigned long drarh, drarl;
	int channel = 0;

	channel = c->channel;

	/* DCR is set by the card. */
	printk("desc : %p => %lx\n", c->desc, virt_to_mic_phys(c->desc));
	drarh = SET_SBOX_DRARHI_SIZE(c->desc_count);
	drarh |= SET_SBOX_DRARHI_BA((unsigned long)virt_to_mic_phys(c->desc)
	                            >> 32);
	drarh |= SET_SBOX_DRARHI_PAGE((unsigned long)virt_to_mic_phys(c->desc)
	                              >> 34UL);
	drarh |= SET_SBOX_DRARHI_SYS(1);
	drarl = (unsigned long)virt_to_mic_phys(c->desc) & 0xffffffff;

	sbox_dma_write(c, SBOX_DRAR_LO_0, drarl);
	sbox_dma_write(c, SBOX_DRAR_HI_0, drarh);
	
	sbox_dma_write(c, SBOX_DTPR_0, 0);
	sbox_dma_write(c, SBOX_DHPR_0, 0);

	c->head = c->tail = 0;
}

void __knf_dma_init(struct knf_device_data *kdd)
{
	/* Host only uses channels >= 4 */
	struct knf_dma_channel *channels = kdd->channels;
	void *ring;

	memset(kdd->channels, 0, sizeof(kdd->channels));

	ring = (void *)__get_free_page(GFP_KERNEL);

	spin_lock_init(&channels[4].lock);
	channels[4].kdd = kdd;
	channels[4].channel = 4;
	channels[4].owner = 1;
	channels[4].desc = ring;
	channels[4].desc_count = PAGE_SIZE / sizeof(union md_mic_dma_desc);
	__initialize_dma(channels + 4);
}

void __knf_dma_finalize(struct knf_device_data *kdd)
{
	if (kdd->channels[4].desc) {
		free_page((unsigned long)kdd->channels[4].desc);
	}
}

unsigned long long st0, st1, st2, ed;

static void __debug_print_dma_reg(struct knf_dma_channel *c)
{
	printk("Channel %d:\n", c->channel);
	printk("DRAR-HI : %x, LO : %x\n", sbox_dma_read(c, SBOX_DRAR_HI_0),
	        sbox_dma_read(c, SBOX_DRAR_LO_0));
	printk("DTPR : %x, DHPR : %x\n", sbox_dma_read(c, SBOX_DTPR_0),
	        sbox_dma_read(c, SBOX_DHPR_0));
}

int __knf_dma_request(struct knf_device_data *kdd, int channel,
                      struct aal_dma_request *req)
{
	unsigned long flags;
	struct knf_dma_channel *c;
	int i, cdesc, ndesc = 1, size;
	union md_mic_dma_desc *desc;

	c = &kdd->channels[channel + 4];
	if (!c->desc) {
		return -EINVAL;
	}
	/* 64K per one desc */
	cdesc = (req->size + 65535) >> 16;
	ndesc = cdesc;
	if (req->callback || req->notify) {
		ndesc++;
	}
	
	spin_lock_irqsave(&c->lock, flags);
	if (!__knf_desc_check_room(c, ndesc)) {
		spin_unlock_irqrestore(&c->lock, flags);
		return -EBUSY;
	}

	size = req->size >> 6;
	
	for (i = 0; i < cdesc; i++) {
		desc = __knf_desc_proceed_head(c);
		desc->desc.memcpy.type = 1;
		if (req->src_os) {
			desc->desc.memcpy.sap = req->src_phys;
		} else {
			desc->desc.memcpy.sap = phys_to_mic_phys(req->src_phys);
		}
		if (req->dest_os) {
			desc->desc.memcpy.dap = req->dest_phys;
		} else {
			desc->desc.memcpy.dap = 
				phys_to_mic_phys(req->dest_phys);
		}

		desc->desc.memcpy.sap += i << 16;
		desc->desc.memcpy.dap += i << 16;

		if (size > 1024) {
			desc->desc.memcpy.length = 1024;
			size -= 1024;
		} else { 
			desc->desc.memcpy.length = size;
		}
	}

	if (req->callback || req->notify) {
		desc = __knf_desc_proceed_head(c);
		desc->desc.status.type = 2;
		if (req->callback) {
			desc->desc.status.intr = 1;
		} else {
			if (req->notify_os) {
				desc->desc.status.dap = 
					(unsigned long)req->notify;
			} else {
				desc->desc.status.dap =
					phys_to_mic_phys(
						(unsigned long)req->notify);
			}
			desc->desc.status.data = (unsigned long)req->priv;
		}
	}
	rdtscll(st1);
	sbox_dma_write(c, SBOX_DHPR_0, c->head);
	
	spin_unlock_irqrestore(&c->lock, flags);

	return 0;
}

int __knf_dma_test(struct knf_device_data *kdd, unsigned long arg)
{
	unsigned long fin = 0;
	struct aal_dma_request req;
	unsigned long to;
	int loop = 0;
	unsigned long *buf;

	if (arg > 4 * 1048576) {
		return -ENOMEM;
	}

	buf = (void *)__get_free_pages(GFP_KERNEL, 10);
	if (!buf) {
		return -ENOMEM;
	}

	rdtscll(st0);
	memset(&req, 0, sizeof(req));

	req.src_phys = 0x40000000;
	req.src_os = kdd;
	req.dest_phys = virt_to_phys(buf);

	req.size = arg;
	req.notify = (void *)virt_to_phys(&fin);
	req.priv = (void *)29;

	__knf_dma_request(kdd, 0, &req);
	rdtscll(st2);

	to = st2 + 1024UL * 1024 * 1024 * 3;

	while (!fin) {
		cpu_relax();
		loop++;
		rdtscll(ed);
		if (ed > to) {
			printk("Timeout\n");
			break;
		}
	}

	rdtscll(ed);

	printk("TSC: %lld, %lld, %lld\n",
	       st1 - st0, st2 - st0, ed - st0);
	printk("Fin : %lx (%d)\n", fin, loop);

	__debug_print_dma_reg(kdd->channels + 4);

	free_pages((unsigned long)buf, 10);

	return ed - st2;
}

static int knf_dma_request(aal_dma_channel_t channel, struct aal_dma_request *r)
{
	__knf_dma_request(channel->priv, channel->channel, r);

	return 0;
}

struct aal_dma_ops knf_dma_ops = {
	.request = knf_dma_request,
};

aal_dma_channel_t knf_aal_get_dma_channel(aal_device_t dev, void *priv,
                                          int channel)
{
	struct knf_device_data *data = priv;

	if (channel < 0 || channel >= KNF_DMA_CHANNELS - 4) {
		return NULL;
	}

	data->aal_channels[channel].dev = dev;
	data->aal_channels[channel].priv = priv;
	data->aal_channels[channel].channel = channel;
	data->aal_channels[channel].ops = &knf_dma_ops;

	return &data->aal_channels[channel];
}

