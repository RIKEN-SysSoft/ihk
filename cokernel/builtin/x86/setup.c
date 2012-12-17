#include <ihk/debug.h>
#include <ihk/mm.h>
#include <ihk/cpu.h>
#include <ihk/perfctr.h>
#include <errno.h>
#include <registers.h>
#include "bootparam.h"

/* MEE Setup.c */
static unsigned char stack[8192] __attribute__((aligned(4096)));

unsigned long boot_param_pa;
struct shimos_boot_param *boot_param;

extern void main(void);
extern void setup_x86(void);
extern struct aal_kmsg_buf kmsg_buf;

unsigned long x86_kernel_phys_base;

void arch_start(unsigned long param_addr, unsigned long phys_address)
{
	x86_kernel_phys_base = phys_address;
	boot_param = phys_to_virt(param_addr);
	boot_param_pa = param_addr;

	/* Set up initial (temporary) stack */
	asm volatile("movq %0, %%rsp" : : "r" (stack + sizeof(stack)));

	main();

	while (1);
}

static struct aal_mc_cpu_info *aal_cpu_info;

static void build_aal_cpu_info(void)
{
	int i, n = 0;

	aal_cpu_info = early_alloc_page();
	aal_cpu_info->hw_ids = (int *)(aal_cpu_info + 1);
	aal_cpu_info->nodes = (int *)(aal_cpu_info + 1) + 64;

	kprintf("CPU: ");
	for (i = 0; i < sizeof(unsigned long) * 8; i++) {
		if (boot_param->cores & (1UL << i)) {
			aal_cpu_info->hw_ids[n] = i;
			aal_cpu_info->nodes[n] = 0;

			kprintf("%d ", aal_cpu_info->hw_ids[n]);
			n++;
		}
	}
	kprintf("\n");

	aal_cpu_info->ncpus = n;
}

void arch_init(void)
{
	/* Ack boot (trampoline code shall be free'd) */
	boot_param->msg_buffer = virt_to_phys(&kmsg_buf);
	boot_param->status = 1;

	build_aal_cpu_info();

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

struct aal_mc_cpu_info *aal_mc_get_cpu_info(void)
{
	return aal_cpu_info;
}

unsigned long get_transit_page_table(void)
{
	return boot_param->ident_table;
}

void __reserve_arch_pages(unsigned long start, unsigned long end,
                          void (*cb)(unsigned long, unsigned long, int))
{
	/* No hole */
}

extern void x86_issue_ipi(int, int);
int aal_mc_interrupt_host(int cpu, int vector)
{
	x86_issue_ipi(cpu, 0xf1);
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

char *aal_mc_get_kernel_args(void)
{
	return boot_param->kernel_args;
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

void aal_mc_setup_dma(void)
{
}

void arch_delay(int us)
{
	unsigned long tsc;

	/* XXX: 3GHz */
	tsc = rdtsc() + 333 * us;
	while (rdtsc() < tsc) {
		cpu_pause();
	}
}

void x86_set_warm_reset(void)
{
	asm volatile("outb %0, $0x70" : : "a"((char)0xf));
	asm volatile("outb %0, $0x71" : : "a"((char)0xa));
}

void mee_mc_dma_init(unsigned long cfg_addr);

void aal_mc_dma_init(void)
{
	mee_mc_dma_init(boot_param->dma_address);
}

static unsigned int perf_map_nehalem[] = 
{
	[APT_TYPE_INSTRUCTIONS]  = CVAL(0xc0, 0x00),
	[APT_TYPE_L1D_REQUEST]   = CVAL(0x43, 0x01),
	[APT_TYPE_L1I_REQUEST]   = CVAL(0x80, 0x03),
	[APT_TYPE_L1D_MISS]      = CVAL(0x51, 0x01),
	[APT_TYPE_L1I_MISS]      = CVAL(0x80, 0x02),
	[APT_TYPE_L2_MISS]       = CVAL(0x24, 0xaa),
	[APT_TYPE_LLC_MISS]      = CVAL(0x2e, 0x41),
	[APT_TYPE_DTLB_MISS]     = CVAL(0x49, 0x01),
	[APT_TYPE_ITLB_MISS]     = CVAL(0x85, 0x01),
	[APT_TYPE_STALL]         = CVAL2(0x0e, 0x01, 1, 1),
	[APT_TYPE_CYCLE]         = CVAL(0x3c, 0x00),
	[PERFCTR_MAX_TYPE] = -1,
};

unsigned int *x86_march_perfmap = perf_map_nehalem;

