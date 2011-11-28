/*
 * MEE DMA Core Main Program
 * Copyright (C) 2011 Taku Shimosawa <shimosawa@is.s.u-tokyo.ac.jp>
 */
#include "mee_dma.h"
#include <linux/sched.h>
#include <linux/mm.h>
#include <aal/aal_host_driver.h>

struct mee_dma_config_struct mee_dma_config;
int mee_dma_intr_status;

int mee_dma_intr_count;
int mee_dma_intr_issued;

/*
 * The functions below run in the same memory space as Linux kernel.
 * It also has the ident page table.
 * But, please note that this CPU is not recognized by Linux, and calling
 * complex Linux functions might cause unexpected results.
 */
void shimos_issue_ipi(int apicid, int vector);

void shimos_dma_interrupt_handler(void)
{
	ack_APIC_irq();

	mee_dma_intr_count++;
}

static inline unsigned long __next(struct mee_dma_channel *c, unsigned long t)
{
	t++;
	if (t >= c->len) {
		t = 0;
	}
	return t;
}

static void __do_interrupt(int flag)
{
	mee_dma_intr_status = 1;
	shimos_issue_ipi(flag & 0xffff, SHIMOS_VECTOR);
}

static void shimos_dma_process(struct mee_dma_desc *desc)
{
	if (desc->type == 1) {
		memcpy(desc->param3, desc->param2, desc->param4);
	} else if (desc->type == 2) {
		*(unsigned long *)(desc->param2) = desc->param4;
	}
	if (desc->param1 & MEE_DMA_DESC_PARAM1_INTR) {
 		__do_interrupt(desc->param1);
	}
}

static void __init_lapic(void)
{
	unsigned long baseaddr;

	rdmsrl(MSR_IA32_APICBASE, baseaddr);
	baseaddr |= 0x800;
	wrmsrl(MSR_IA32_APICBASE, baseaddr);

	apic_write(APIC_SPIV, 0x1ff);
}


static void shimos_dma_process_channel(struct mee_dma_channel *channel)
{
	struct mee_dma_desc *desc, *cur;

	desc = (struct mee_dma_desc *)channel->desc_ptr;
	for (; channel->head != channel->tail;
	     channel->tail = __next(channel, channel->tail)) {
		cur = desc + channel->tail;
		shimos_dma_process(cur);
	}
}

void shimos_dma_main(void)
{
	int i;

	__init_lapic();

	mee_dma_config.status = 1;

	asm volatile("sti");

	while (!mee_dma_config.doorbell) {
		halt();
	}

	while (1) {
		mee_dma_config.doorbell = 0;
		while (!mee_dma_config.doorbell) {
			cpu_relax();
		}
		mee_dma_config.doorbell = 0;

		for (i = 0; i < MEE_DMA_CHANNELS; i++) {
			shimos_dma_process_channel(mee_dma_config.channels + i);
		}
	}
}

/*
 * The functions below are called by Linux
 * (where the codes can be written normally)
 */
void mee_dma_desc_init(void)
{
	int i;
	struct mee_dma_channel *c;

	for (i = 0; i < MEE_DMA_CHANNELS; i++) {
		c = mee_dma_config.channels + i;
		c->desc_ptr = virt_to_phys((void *)__get_free_page(GFP_KERNEL));
		c->head = c->tail = 0;
		c->len = PAGE_SIZE / sizeof(struct mee_dma_desc);
		spin_lock_init(&c->lock);
	}
	mee_dma_config.doorbell = 1;
	mee_dma_issue_interrupt();
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

/* Host uses 0 */
int __mee_dma_request(aal_device_t dev, int channel,
                      struct aal_dma_request *req)
{
	unsigned long flags;
	struct mee_dma_channel *c;
	int ndesc = 1;
	struct mee_dma_desc *desc, *desc_head;
	unsigned long h;

	if (channel < 0 || channel >= MEE_DMA_CHANNELS) {
		return -EINVAL;
	}

	c = mee_dma_config.channels + channel;
	
	if (req->callback || req->notify) {
		ndesc++;
	}

	spin_lock_irqsave(&c->lock, flags);

	if (!__mee_desc_check_room(c, ndesc)) {
		spin_unlock_irqrestore(&c->lock, flags);
		return -EBUSY;
	}

	h = c->head;

	desc_head = phys_to_virt(c->desc_ptr);

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
			desc->param1 = cpu_physical_id(smp_processor_id()) |
				MEE_DMA_DESC_PARAM1_INTR;
		} else if(req->notify) {
			desc->param2 = (void *)req->notify;
			desc->param4 = (unsigned long)req->priv;
		}
		h = __next(c, h);
	}

	c->head = h;
	spin_unlock_irqrestore(&c->lock, flags);

	mee_dma_config.doorbell = 1;
	/*	mee_dma_issue_interrupt(); */

	return 0;
}


