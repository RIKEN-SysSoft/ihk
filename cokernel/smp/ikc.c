#include <ihk/cpu.h>
#include <ihk/ikc.h>
#include <ihk/lock.h>
#include <memory.h>
#include <string.h>
#include <llist.h>
#include <kmalloc.h>
#include "bootparam.h"

/* Use Linux IRQ work interrupts */
#ifdef IHK_IKC_USE_LINUX_WORK_IRQ
#define IRQ_WORK_PENDING	1UL
#define IRQ_WORK_BUSY		2UL
#define IRQ_WORK_FLAGS		3UL

struct linux_irq_work {
	unsigned long flags;
	struct llist_node llnode;
	void (*func)(struct linux_irq_work *work);
	char padding[40]; /* cache line sized */
};

struct linux_irq_work *per_cpu_irq_work;

int ihk_mc_interrupt_host(int cpu, int vector)
{
	struct linux_irq_work *work;

	/*
	 * This needs to be malloc()'d so that it has the same
	 * virtual to physical mapping as in Linux
	 */
	if (!per_cpu_irq_work) {
		int cpu;

		per_cpu_irq_work = kmalloc(
				sizeof(struct linux_irq_work) * num_processors,
				IHK_MC_AP_NOWAIT);

		if (!per_cpu_irq_work) {
			kprintf("%s: error: allocating IKC Linux IRQ work\n",
					__func__);
			return -ENOMEM;
		}

		for (cpu = 0; cpu < num_processors; ++cpu) {
			per_cpu_irq_work[cpu].func =
				boot_param->ikc_irq_work_func;
			per_cpu_irq_work[cpu].flags = 0;
		}

		kprintf("Using Linux work IRQ for IKC IPI.\n");
	}

	/*
	 * Each McKernel CPU has it's own dedicated Linux IRQ work
	 * structure that is used every time a Linux CPU is interrupted
	 * for IKC processing. The IRQ_WORK_BUSY flag indicates
	 * if Linux has not yet processed the previous IRQ.
	 */
	work = &per_cpu_irq_work[ihk_mc_get_processor_id()];
	while (work->flags & IRQ_WORK_BUSY) {
		cpu_pause();
	}

	dkprintf("%s: McKernel CPU %d -> Linux CPU %d\n",
			__func__, ihk_mc_get_processor_id(), cpu);

	/* Linux will clear this */
	work->flags = IRQ_WORK_BUSY;
	llist_add(&work->llnode,
			(struct llist_head *)
			boot_param->ihk_ikc_cpu_raised_list[cpu]);

	ihk_mc_ikc_arch_issue_host_ipi(ihk_mc_get_apicid(cpu),
			boot_param->ihk_ikc_irq);
	return 0;
}
#endif // IHK_IKC_USE_LINUX_WORK_IRQ

extern int ihk_mc_ikc_init_first_local(struct ihk_ikc_channel_desc *channel,
		int (*h)(struct ihk_ikc_channel_desc *,
			void *, void *));

int ihk_mc_ikc_init_first(struct ihk_ikc_channel_desc *channel,
		int (*packet_handler)(struct ihk_ikc_channel_desc *,
			void *, void *))
{
	return ihk_mc_ikc_init_first_local(channel, packet_handler);
}

int ihk_ikc_send_interrupt(struct ihk_ikc_channel_desc *channel)
{
	return ihk_mc_interrupt_host(channel->send.intr_cpu,
			IHK_GV_IKC);
}
