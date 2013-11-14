/**
 * \file mic_dma.c
 *  License details are found in the file LICENSE.
 * \brief
 *	IHK MIC Driver: Xeon Phi DMA Device Driver 
 * \author Taku Shimosawa  <shimosawa@is.s.u-tokyo.ac.jp> \par
 *	Copyright (C) 2011-2012 Taku Shimosawa <shimosawa@is.s.u-tokyo.ac.jp>
 */
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
#include <ihk/misc/debug.h>

#include "mic_mmio.h"
#include "mic_user.h"

//#define DMA_DEBUG

#ifdef DMA_DEBUG
#define dprintk printk
#else
#define dprintk(...)
#endif

/** \brief Read DMA-related registers in the SBOX MMIO registers.
 *
 * @param c     Channel
 * @param index Base offset in SBOX of the DMA register to read
 */
static unsigned int sbox_dma_read(struct mic_dma_channel *c, int index)
{
	return mic_read_sbox(c->kdd, index + 0x40 * c->channel);
}

/** \brief Write DMA-related registers in the SBOX MMIO registers.
 *
 * @param c     Channel
 * @param index Base offset in SBOX of the DMA register to write
 * @param value Value to write
 */
static void sbox_dma_write(struct mic_dma_channel *c,
                           int index, unsigned int value)
{
	mic_write_sbox(c->kdd, index + 0x40 * c->channel, value);
}

/** \brief Check if the DMA ring has enough room of the certain size
 *
 * @param c     Channel
 * @param ndesc Number of the descriptors that you want
 */
static char __mic_desc_check_room(struct mic_dma_channel *c, int ndesc)
{
	int h = c->head, t = c->tail; 

#ifdef CONFIG_MIC
	int reg_value = 0;
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
#else
	t = c->tail = sbox_dma_read(c, SBOX_DTPR_0);
	
	if (h == t && ndesc < c->desc_count)
		return 1;
	else if (h < t && ((t - h) > ndesc)) 
		return 1;
	else if (h > t && (c->desc_count - h + t) > ndesc)
		return 1;

	return 0;
#endif
}

/** \brief Proceed the head pointer of the DMA channel, and returns the 
 *  pointer to the descriptor of the original head position.
 * 
 * @param c Channel
 * @return Pointer to the descriptor of the original head position
 */
static union md_mic_dma_desc *__mic_desc_proceed_head(struct mic_dma_channel *c)
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

/** \brief Convert the host physical address to the physical address in MIC
 * 
 * This function converts by very simple calculation, and assumes
 * the straight-forward mapping of the host memory in MIC.
 * @param phys "Host" physical address to convert ( must be < 32GB )
 * @return Physical address in the MIC's address space
 */
static unsigned long phys_to_mic_phys(unsigned long phys)
{
	/* This is true when phys < 32GB or so. */
	return phys + MIC_SYSTEM_BASE;
}

/** \brief Convert the host kernel virtual address to the address in MIC.
 * 
 * The assumption and limitation are the same as phys_to_mic_phys.
 * @param virt "Host" virtual address to convert ( must be < 32GB in physical )
 * @return Physical address in the MIC's address space
 */
static unsigned long virt_to_mic_phys(void *virt)
{
	return phys_to_mic_phys(virt_to_phys(virt));
}

/** \brief Initialize a DMA channel
 * 
 * This function initializes DMA-related registers for the specified channel.
 * It is assumed that the MIC kernel initializes the DCR register.
 * @param c DMA channel
 */
static void __initialize_dma(struct mic_dma_channel *c)
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
	
#ifdef CONFIG_MIC
	sbox_dma_write(c, SBOX_DTPR_0, 0);
	sbox_dma_write(c, SBOX_DHPR_0, 0);

	c->head = c->tail = 0;
#else
	c->tail = sbox_dma_read(c, SBOX_DTPR_0);
	c->head = c->tail;
	sbox_dma_write(c, SBOX_DHPR_0, c->head);	
#endif
}

/** \brief Initialize the DMA registers for host use
 * 
 * Currently, this function initializes only the Channel 4 of the DMA engine
 * of the Knights Ferry device.
 * @param kdd A Knights Ferry device
 */
void __mic_reset_dma_registers(struct mic_device_data *kdd)
{
	__initialize_dma(kdd->channels + 4);
}

/** \brief Initialize the DMA devices for host use.
 * 
 * The function initializes the channel descriptor structures, and
 * DMA registers.
 * @param kdd A Knights Ferry device
 */
void __mic_dma_init(struct mic_device_data *kdd)
{
	/* Host only uses channels >= 4 */
	struct mic_dma_channel *channels = kdd->channels;
	void *ring;

	memset(kdd->channels, 0, sizeof(kdd->channels));

	ring = (void *)__get_free_page(GFP_KERNEL);

	spin_lock_init(&channels[4].lock);
	channels[4].kdd = kdd;
	channels[4].channel = 4;
	channels[4].owner = 1;
	channels[4].desc = ring;
	channels[4].desc_count = PAGE_SIZE / sizeof(union md_mic_dma_desc);

	__mic_reset_dma_registers(kdd);
}

/** \brief Deinitialize the DMA devices for host use.
 * 
 * The function finalizes the channel descriptor structures.
 * @param kdd A Knights Ferry device
 */
void __mic_dma_finalize(struct mic_device_data *kdd)
{
	if (kdd->channels[4].desc) {
		free_page((unsigned long)kdd->channels[4].desc);
	}
}

unsigned long long st0, st1, st2, ed;

static void __debug_print_dma_reg(struct mic_dma_channel *c)
{
	printk("Channel %d:\n", c->channel);
	printk("DRAR-HI : %x, LO : %x\n", sbox_dma_read(c, SBOX_DRAR_HI_0),
	        sbox_dma_read(c, SBOX_DRAR_LO_0));
	printk("DTPR : %x, DHPR : %x\n", sbox_dma_read(c, SBOX_DTPR_0),
	        sbox_dma_read(c, SBOX_DHPR_0));
}

/** \brief Core function to perform a DMA request
 * 
 * @param kdd     A Knights Ferry device
 * @param channel Channel number
 * @param req     DMA request descriptor
 */
int __mic_dma_request(struct mic_device_data *kdd, int channel,
                      struct ihk_dma_request *req)
{
	unsigned long flags;
	struct mic_dma_channel *c;
	int i, cdesc, ndesc = 1, size;
	union md_mic_dma_desc *desc = NULL;

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
	if (!__mic_desc_check_room(c, ndesc)) {
		spin_unlock_irqrestore(&c->lock, flags);
		return -EBUSY;
	}

	size = (req->size + 63) >> 6;
	
	for (i = 0; i < cdesc; i++) {
		desc = __mic_desc_proceed_head(c);
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
	dprintk("COPY: src = %lx, dest = %lx, size = %d (org = %ld)\n",
	       (unsigned long)desc->desc.memcpy.sap, 
		   (unsigned long)desc->desc.memcpy.dap,
	       desc->desc.memcpy.length, req->size);

	if (req->callback || req->notify) {
		desc = __mic_desc_proceed_head(c);
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
		dprintk("STATUS: dest = %lx, data = %ld\n",
		       (unsigned long)desc->desc.status.dap, 
			   (unsigned long)desc->desc.status.data);
	}
	
	rdtscll(st1);
	sbox_dma_write(c, SBOX_DHPR_0, c->head);
	
	spin_unlock_irqrestore(&c->lock, flags);

	return 0;
}

int __mic_dma_test(struct mic_device_data *kdd, unsigned long arg)
{
	unsigned long fin = 0;
	struct ihk_dma_request req;
	unsigned long to;
	int loop = 0;
	unsigned long *buf;
	ihk_dma_channel_t dma_channel;

	if (arg > 4 * 1048576) {
		return -ENOMEM;
	}
	dma_channel = ihk_device_get_dma_channel(kdd->ihk_dev, 0);
	if (!dma_channel) {
		return -EINVAL;
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

	ihk_dma_request(dma_channel, &req);
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

/** \brief Wrapper function of __mic_dma_request
 * 
 * @param channel IHK DMA channel structure
 * @param r       DMA request descriptor
 */
static int mic_dma_request(ihk_dma_channel_t channel, struct ihk_dma_request *r)
{
	__mic_dma_request(channel->priv, channel->channel, r);

	return 0;
}

struct ihk_dma_ops mic_dma_ops = {
	.request = mic_dma_request,
};

/** \brief ihk_host_get_dma_channel implementation
 * 
 *  Note that the "channel" parameter has the offset of 4, that is,
 *  channel = 0 indicates that Channel 4 in the hardware DMA engine.
 */
ihk_dma_channel_t mic_ihk_get_dma_channel(ihk_device_t dev, void *priv,
                                          int channel)
{
	struct mic_device_data *data = priv;

	if (channel < 0 || channel >= MIC_DMA_CHANNELS - 4) {
		return NULL;
	}

	data->ihk_channels[channel].dev = dev;
	data->ihk_channels[channel].priv = priv;
	data->ihk_channels[channel].channel = channel;
	data->ihk_channels[channel].ops = &mic_dma_ops;

	return &data->ihk_channels[channel];
}

