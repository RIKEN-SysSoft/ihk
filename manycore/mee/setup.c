#include <aal/debug.h>
#include <aal/mm.h>
#include <aal/cpu.h>
#include <errno.h>
#include "bootparam.h"

/* MEE Setup.c */
static unsigned char stack[8192] __attribute__((aligned(4096)));

unsigned long boot_param_pa;
struct shimos_boot_param *boot_param;

extern void main(void);
extern void setup_x86(void);
extern struct aal_kmsg_buf kmsg_buf;

void arch_start(unsigned long param_addr)
{
	boot_param = (struct shimos_boot_param *)param_addr;
	boot_param_pa = param_addr;

	/* Set up initial (temporary) stack */
	asm volatile("movq %0, %%rsp" : : "r" (stack + sizeof(stack)));

	main();

	while (1);
}

void arch_init(void)
{
	/* Ack boot (trampoline code shall be free'd) */
	boot_param->msg_buffer = (unsigned long)&kmsg_buf;
	boot_param->status = 1;

	setup_x86();
	boot_param = map_fixed_area(boot_param_pa, sizeof(*boot_param), 0);
}

void arch_ready(void)
{
	/* Make it ready */
	boot_param->status = 2;
}

void arch_set_mikc_queue(void *rq, void *wq)
{
	boot_param->mikc_queue_recv = virt_to_phys(wq);
	boot_param->mikc_queue_send = virt_to_phys(rq);
}

unsigned long aal_mc_get_memory_address(enum aal_mc_gma_type type, int opt)
{
	switch (type) {
	case AAL_MC_GMA_MAP_START:
	case AAL_MC_GMA_AVAIL_START:
		return boot_param->start;
	case AAL_MC_GMA_MAP_END:
	case AAL_MC_GMA_AVAIL_END:
		return boot_param->end;
	case AAL_MC_GMA_HEAP_START:
		return virt_to_phys(get_last_early_heap());
	}

	return -ENOENT;
}

void __reserve_arch_pages(unsigned long start, unsigned long end,
                          void (*cb)(unsigned long, unsigned long, int))
{
	/* No hole */
}

extern void x86_issue_ipi(int, int);
int aal_mc_interrupt_host(int vector)
{
	x86_issue_ipi(0, 0xf1);
	return 0;
}

int aal_mc_get_vector(enum aal_mc_gv_type type)
{

	switch (type) {
	case AAL_GV_IKC:
		return 0xd1;
	default:
		return -ENOENT;
	}
}


unsigned long aal_mc_map_memory(void *os, unsigned long phys,
                                unsigned long size)
{
	/* TODO: os support (currently, os is ignored and assumed to be Host) */
	return phys;
}

void aal_mc_unmap_memory(void *os, unsigned long phys, unsigned long size)
{
	return;
}
