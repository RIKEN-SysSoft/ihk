/**
 * \file dmacore.c
 * \brief
 *	IHK BUILTIN Driver: BUILTIN DMA Core Main Program
 * \author Taku Shimosawa  <shimosawa@is.s.u-tokyo.ac.jp> \par
 *	Copyright (C) 2011-2012 Taku Shimosawa <shimosawa@is.s.u-tokyo.ac.jp>
 */
#ifdef USE_DMA
#include "builtin_dma.h"
#include <linux/sched.h>
#include <linux/mm.h>
#include <asm/io.h>
#include <ihk/ihk_host_driver.h>


/** \brief Pointer to the structure that contains information of the DMA core */
struct builtin_dma_config_struct *builtin_dma_config;
/** \brief Status of the current interrupt. (just for debug) */
int builtin_dma_intr_status;

/** \brief Number of DMA interrupts handled. (just for debug) */
int builtin_dma_intr_count;
/** \brief Number of DMA interrupts issued. (just for debug) */
int builtin_dma_intr_issued;

/*
 * The functions below run in the same memory space as Linux kernel.
 * It also has the ident page table.
 * But, please note that this CPU is not recognized by Linux, and calling
 * complex Linux functions might cause unexpected results.
 */
extern void shimos_issue_ipi(int apicid, int vector);


/** \brief Interrupt handler of the DMA core */
void shimos_dma_interrupt_handler(void)
{
	ack_APIC_irq();

	builtin_dma_intr_count++;
}

/** \brief Get the next index in a ring buffer of the DMA channel */
static inline unsigned long __next(struct builtin_dma_channel *c, unsigned long t)
{
	t++;
	if (t >= c->len) {
		t = 0;
	}
	return t;
}

/** \brief Issues an interrupt to the host kernel */
static void __do_interrupt(int flag)
{
	builtin_dma_intr_status = 1;
	shimos_issue_ipi(flag & 0xffff, SHIMOS_VECTOR);
}

/** \brief Processes a DMA request in the DMA channel ring */
static void shimos_dma_process(struct builtin_dma_desc *desc)
{
	if (desc->type == 1) {
		memcpy(desc->param3, desc->param2, desc->param4);
	} else if (desc->type == 2) {
		*(unsigned long *)(desc->param2) = desc->param4;
	}
	if (desc->param1 & BUILTIN_DMA_DESC_PARAM1_INTR) {
 		__do_interrupt(desc->param1);
	}
}

/** \brief Enable the local APIC of the DMA core */
static void __init_lapic(void)
{
	unsigned long baseaddr;

	rdmsrl(MSR_IA32_APICBASE, baseaddr);
	baseaddr |= 0x800;
	wrmsrl(MSR_IA32_APICBASE, baseaddr);

	apic_write(APIC_SPIV, 0x1ff);
}


/** \brief Process all the descriptors in the ring buffer of the channel */
static void shimos_dma_process_channel(struct builtin_dma_channel *channel)
{
	struct builtin_dma_desc *desc, *cur;

	desc = (struct builtin_dma_desc *)channel->desc_ptr;
	for (; channel->head != channel->tail;
	     channel->tail = __next(channel, channel->tail)) {
		cur = desc + channel->tail;
		shimos_dma_process(cur);
	}
}

/** \brief Main routine of the DMA core */
void shimos_dma_main(void)
{
	int i;

	__init_lapic();

	builtin_dma_config->status = 1;

	asm volatile("sti");

	while (!builtin_dma_config->doorbell) {
		halt();
	}

	builtin_dma_config->doorbell = 0;
	while (1) {
		while (!builtin_dma_config->doorbell) {
			cpu_relax();
		}
		builtin_dma_config->doorbell = 0;
		mb();
		for (i = 0; i < BUILTIN_DMA_CHANNELS; i++) {
			shimos_dma_process_channel(builtin_dma_config->channels
			                           + i);
		}
	}
}

/*
 * The functions below are called by Linux
 * (where the codes can be written normally)
 */
/** \brief Initializes the DMA channels */
void builtin_dma_desc_init(void)
{
	int i;
	struct builtin_dma_channel *c;

	for (i = 0; i < BUILTIN_DMA_CHANNELS; i++) {
		c = builtin_dma_config->channels + i;
		printk("DMA Channels (%d): %lx\n", i, (long)virt_to_phys(c));
		c->desc_ptr = virt_to_phys((void *)__get_free_page(GFP_KERNEL));
		c->head = c->tail = 0;
		c->len = PAGE_SIZE / sizeof(struct builtin_dma_desc);
		spin_lock_init(&c->lock);
		printk("Desc Ptr: %lx\n", c->desc_ptr);
	}

	builtin_dma_config->doorbell = 1;
	builtin_dma_issue_interrupt();
}

/** \brief Check if there is room enough to put the desired number of
 * DMA descriptors */
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

/** \brief Request the BUILTIN DMA core to perform the DMA request */
int __builtin_dma_request(ihk_device_t dev, int channel,
                      struct ihk_dma_request *req)
{
	unsigned long flags;
	struct builtin_dma_channel *c;
	int ndesc = 1;
	struct builtin_dma_desc *desc, *desc_head;
	unsigned long h;

	if (channel < 0 || channel >= BUILTIN_DMA_CHANNELS) {
		return -EINVAL;
	}

	c = builtin_dma_config->channels + channel;
	
	if (req->callback || req->notify) {
		ndesc++;
	}

	spin_lock_irqsave(&c->lock, flags);

	if (!__builtin_desc_check_room(c, ndesc)) {
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
				BUILTIN_DMA_DESC_PARAM1_INTR;
		} else if(req->notify) {
			desc->param2 = (void *)req->notify;
			desc->param4 = (unsigned long)req->priv;
		}
		h = __next(c, h);
	}

	c->head = h;
	spin_unlock_irqrestore(&c->lock, flags);

	builtin_dma_config->doorbell = 1;
	/*	builtin_dma_issue_interrupt(); */

	return 0;
}
#endif
