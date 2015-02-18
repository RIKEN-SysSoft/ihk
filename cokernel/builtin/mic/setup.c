#include <ihk/debug.h>
#include <ihk/mm.h>
#include <ihk/cpu.h>
#include <ihk/perfctr.h>
#include <errno.h>
#include <registers.h>
#include <march.h>
#include "bootparam.h"

/* BUILTIN Setup.c */
static unsigned char stack[8192] __attribute__((aligned(4096)));

unsigned long boot_param_pa;
struct shimos_boot_param *boot_param;

extern void main(void);
extern void setup_x86(void);
extern void init_boot_processor_local(void);
extern struct ihk_kmsg_buf kmsg_buf;

unsigned long x86_kernel_phys_base;

static int cpumhz = 1053;

void set_cpumhz(int mhzval)
{
        cpumhz = mhzval;
        kprintf("set cpu MHz: %d\n", cpumhz);
}

unsigned long ap_trampoline = 0;
unsigned int ihk_ikc_irq = 0;
unsigned int ihk_ikc_irq_apicid = 0;

/* NOTEs on parameters: 
 *
 * param_addr (RDI) is set in shimos_trampoline_64.S before jumping
 * into starrtup.S 
 * phys_addr (RSI), ap_trampoline_start (RDX), ihk_ikc_irq (RCX) are set 
 * in startup.S
 */
void arch_start(unsigned long param_addr, unsigned long phys_address, 
	unsigned long _ap_trampoline, unsigned long _ihk_ikc_irq)
{
	x86_kernel_phys_base = phys_address;
	boot_param = phys_to_virt(param_addr);
	boot_param_pa = param_addr;
	ap_trampoline = _ap_trampoline;
	ihk_ikc_irq = _ihk_ikc_irq & 0x00000000ffffffff;
	ihk_ikc_irq_apicid = (_ihk_ikc_irq >> 32);

	/* Set up initial (temporary) stack */
	asm volatile("movq %0, %%rsp" : : "r" (stack + sizeof(stack)));

	init_boot_processor_local();
	main();

	while (1);
}

static struct ihk_mc_cpu_info *ihk_cpu_info;

static void build_ihk_cpu_info(void)
{
	int i, n = 0;
	unsigned long s;

	ihk_cpu_info = early_alloc_page();
	ihk_cpu_info->hw_ids = (int *)(ihk_cpu_info + 1);
	ihk_cpu_info->nodes = (int *)(ihk_cpu_info + 1) + SHIMOS_MAX_CORES;

	kprintf("CPU: ");
	s = kprintf_lock();
	for (i = 0; i < SHIMOS_MAX_CORES; i++) {
		if (CORE_ISSET(i, boot_param->coreset)) {
			ihk_cpu_info->hw_ids[n] = i;
			ihk_cpu_info->nodes[n] = 0;

			__kprintf("%d ", ihk_cpu_info->hw_ids[n]);
			n++;
		}
	}
	__kprintf("\n");
	kprintf_unlock(s);

	ihk_cpu_info->ncpus = n;
}

void arch_init(void)
{
	/* Ack boot (trampoline code shall be free'd) */
	boot_param->msg_buffer = virt_to_phys(&kmsg_buf);
	boot_param->status = 1;

	build_ihk_cpu_info();

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

unsigned long ihk_mc_get_memory_address(enum ihk_mc_gma_type type, int opt)
{
	switch (type) {
	case IHK_MC_GMA_MAP_START:
	case IHK_MC_GMA_AVAIL_START:
		return boot_param->start;

	case IHK_MC_GMA_MAP_END:
	case IHK_MC_GMA_AVAIL_END:
		return boot_param->end;

	case IHK_MC_GMA_HEAP_START:
		return virt_to_phys(get_last_early_heap());

	case IHK_MC_NR_RESERVED_AREAS:
		return 0;

	case IHK_MC_RESERVED_AREA_START:
	case IHK_MC_RESERVED_AREA_END:
		return -ENOENT;
	}

	return -ENOENT;
}

struct ihk_mc_cpu_info *ihk_mc_get_cpu_info(void)
{
	return ihk_cpu_info;
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
int ihk_mc_interrupt_host(int cpu, int vector)
{
	x86_issue_ipi(cpu, 0xee);
	return 0;
}

int ihk_mc_get_vector(enum ihk_mc_gv_type type)
{

	switch (type) {
	case IHK_GV_IKC:
		return 0xd1;
	default:
		return -ENOENT;
	}
}

char *ihk_mc_get_kernel_args(void)
{
	return boot_param->kernel_args;
}

unsigned long ihk_mc_map_memory(void *os, unsigned long phys,
                                unsigned long size)
{
	/* TODO: os support (currently, os is ignored and assumed to be Host) */
	return phys;
}

void ihk_mc_unmap_memory(void *os, unsigned long phys, unsigned long size)
{
	return;
}

void ihk_mc_setup_dma(void)
{
}

void arch_delay(int us)
{
	unsigned long tsc;

	/* XXX: 1.2 */
/*	tsc = rdtsc() + 833 * us; */
	tsc = rdtsc() + cpumhz * us;
	while (rdtsc() < tsc) {
		cpu_pause();
	}
}

void x86_set_warm_reset(unsigned long ip, char *first_page_va)
{
#if 0	/* Should not be necessary because it uses the SIPI */
	/* Write CMOS */
	asm volatile("outb %0, $0x70" : : "a"((char)0xf));
	asm volatile("outb %0, $0x71" : : "a"((char)0xa));

	/* Set vector */
	*(unsigned short *)(first_page_va + 0x469) = (ip >> 4);
	*(unsigned short *)(first_page_va + 0x467) = ip & 0xf;
#endif
}

void builtin_mc_dma_init(unsigned long cfg_addr);

void ihk_mc_dma_init(void)
{
	builtin_mc_dma_init(boot_param->dma_address);
}

static unsigned int perf_map_mic[] = 
{
	[APT_TYPE_DATA_PAGE_WALK]                  = CVAL(0x02, 0x00),
	[APT_TYPE_DATA_READ_MISS]                  = CVAL(0x03, 0x00),
	[APT_TYPE_DATA_WRITE_MISS]                 = CVAL(0x04, 0x00),
	[APT_TYPE_BANK_CONFLICTS]                  = CVAL(0x0a, 0x00),

	[APT_TYPE_CODE_CACHE_MISS]                 = CVAL(0x0e, 0x00),
	[APT_TYPE_INSTRUCTIONS_EXECUTED]           = CVAL(0x16, 0x00),
	[APT_TYPE_INSTRUCTIONS_EXECUTED_V_PIPE]    = CVAL(0x17, 0x00),

	[APT_TYPE_L2_READ_MISS]                    = CVAL(0xcb, 0x10),
	[APT_TYPE_L2_CODE_READ_MISS_CACHE_FILL]    = CVAL(0xf0, 0x10),
	[APT_TYPE_L2_DATA_READ_MISS_CACHE_FILL]    = CVAL(0xf1, 0x10),
	[APT_TYPE_L2_CODE_READ_MISS_MEM_FILL]      = CVAL(0xf5, 0x10),
	[APT_TYPE_L2_DATA_READ_MISS_MEM_FILL]      = CVAL(0xf6, 0x10),

	[PERFCTR_MAX_TYPE] = -1,
};

unsigned int *x86_march_perfmap = perf_map_mic;

void x86_march_perfctr_start(unsigned long counter_mask)
{
	wrmsr(MSR_PERF_FLT_MASK, 0);
}
