#include <ihk/debug.h>
#include <ihk/mm.h>
#include <ihk/cpu.h>
#include <ihk/perfctr.h>
#include <errno.h>
#include <registers.h>
#include "bootparam.h"

/* BUILTIN Setup.c */
static unsigned char stack[8192] __attribute__((aligned(4096)));

unsigned long boot_param_pa;
struct smp_boot_param *boot_param;
unsigned long bootstrap_mem_end;
static int boot_param_size;

extern void main(void);
extern void setup_x86(void);
extern void init_boot_processor_local(void);
extern struct ihk_kmsg_buf kmsg_buf;
extern int no_turbo;

unsigned long x86_kernel_phys_base;
unsigned long ap_trampoline = 0;
unsigned int ihk_ikc_irq = 0;
unsigned int ihk_ikc_irq_apicid = 0;

/* NOTEs on parameters: 
 *
 * param_addr (RDI) is set in shimos_trampoline_64.S before jumping
 * into starrtup.S 
 * phys_addr (RSI), ap_trampoline_start (RDX) are set in startup.S
 */
void arch_start(unsigned long param_addr, unsigned long phys_address, 
	unsigned long _ap_trampoline)
{
	x86_kernel_phys_base = phys_address;
	boot_param = phys_to_virt(param_addr);
	boot_param_pa = param_addr;
	ap_trampoline = _ap_trampoline;
	ihk_ikc_irq = boot_param->ihk_ikc_irq;
	bootstrap_mem_end = boot_param->bootstrap_mem_end;
	boot_param_size = boot_param->param_size;

	/* Set up initial (temporary) stack */
	asm volatile("movq %0, %%rsp" : : "r" (stack + sizeof(stack)));

	init_boot_processor_local();
	main();

	while (1);
}

static struct ihk_mc_cpu_info *ihk_cpu_info = NULL;

static void build_ihk_cpu_info(void)
{
	int i;
	struct ihk_smp_boot_param_cpu *bp_cpu;

	ihk_cpu_info = early_alloc_pages((
				(sizeof(*ihk_cpu_info) + boot_param->nr_cpus *
				 (sizeof(ihk_cpu_info->hw_ids) + sizeof(ihk_cpu_info->nodes) + 
                  sizeof(ihk_cpu_info->linux_cpu_ids) + sizeof(ihk_cpu_info->ikc_cpus)) +
				PAGE_SIZE - 1) >> PAGE_SHIFT));
	ihk_cpu_info->hw_ids = (int *)(ihk_cpu_info + 1);
	ihk_cpu_info->nodes = (int *)(ihk_cpu_info + 1) + boot_param->nr_cpus;
	ihk_cpu_info->linux_cpu_ids = (int *)(ihk_cpu_info->nodes) + boot_param->nr_cpus;
	ihk_cpu_info->ikc_cpus = (int *)(ihk_cpu_info->linux_cpu_ids) + boot_param->nr_cpus;

	bp_cpu = (struct ihk_smp_boot_param_cpu *)(boot_param + 1);
	for (i = 0; i < boot_param->nr_cpus; ++i) {
		ihk_cpu_info->hw_ids[i] = bp_cpu->hw_id;
		ihk_cpu_info->nodes[i] = bp_cpu->numa_id;
		ihk_cpu_info->linux_cpu_ids[i] = bp_cpu->linux_cpu_id;
		ihk_cpu_info->ikc_cpus[i] = bp_cpu->ikc_cpu;
		++bp_cpu;
	}

	ihk_cpu_info->ncpus = boot_param->nr_cpus;
}

int ihk_mc_get_numa_id(void)
{
	if (ihk_cpu_info) {
		return ihk_cpu_info->nodes[ihk_mc_get_processor_id()];
	}
	else {
		return 0;
	}
}

extern char *strstr(const char *haystack, const char *needle);

void arch_init(void)
{
	/* Ack boot (trampoline code shall be free'd) */
	boot_param->status = 1;

	/* This is an early check to instruct the kernel initialization 
	 * process not to deal with turbo boost support */
	if (strstr(boot_param->kernel_args, "turbo")) {
		no_turbo = 0;
	}

	setup_x86();
	kprintf("boot_param_size: %lu\n", boot_param_size);

	/* Remap boot parameter structure */
	boot_param = map_fixed_area(boot_param_pa, boot_param_size, 0);

	kprintf("ns_per_tsc: %lu\n", boot_param->ns_per_tsc);
	build_ihk_cpu_info();
}

void arch_ready(void)
{
	/* Make it ready */
	boot_param->status = 2;
	barrier();
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

	/* To eliminate compiler warnings.. */
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

extern void (*x86_issue_ipi)(int, int);
int ihk_mc_interrupt_host(int cpu, int vector)
{
	x86_issue_ipi(ihk_mc_get_apicid(cpu), ihk_ikc_irq);
	return 0;
}

int ihk_mc_get_vector(enum ihk_mc_gv_type type)
{

	switch (type) {
	case IHK_GV_IKC:
		return 0xd1;
	case IHK_GV_QUERY_FREE_MEM:
		return 200;
	default:
		return -ENOENT;
	}
}

/* Returns the number of nanosecs in 1000 TSC */
unsigned long ihk_mc_get_ns_per_tsc(void)
{
	return boot_param->ns_per_tsc;
}

void ihk_mc_get_boot_time(unsigned long *tv_sec, unsigned long *tv_nsec)
{
	*tv_sec = boot_param->boot_sec;
	*tv_nsec = boot_param->boot_nsec;
}

char *ihk_get_kargs(void)
{
	return boot_param->kernel_args;
}

int ihk_set_kmsg(unsigned long addr, unsigned long size)
{
	boot_param->msg_buffer = addr;
	boot_param->msg_buffer_size = size;

	return 0;
}	

int ihk_set_monitor(unsigned long addr, unsigned long size)
{
	boot_param->monitor = addr;
	boot_param->monitor_size = size;
	
	return 0;
}

int ihk_set_nmi_mode_addr(unsigned long addr)
{
	boot_param->nmi_mode_addr = addr;
	
	return 0;
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

int ihk_mc_get_nr_numa_nodes(void)
{
	return boot_param->nr_numa_nodes;
}

int ihk_mc_get_numa_node(int id, int *linux_numa_id, int *type)
{
	struct ihk_smp_boot_param_numa_node *node;

	if (id < 0 || id >= boot_param->nr_numa_nodes)
		return -1;

	node = (((struct ihk_smp_boot_param_numa_node *)
		((char *)boot_param + sizeof(*boot_param) + 
		boot_param->nr_cpus * sizeof(struct ihk_smp_boot_param_cpu))) + id);

	if (linux_numa_id) *linux_numa_id = node->linux_numa_id;
	if (type) *type = node->type;

	return 0;
}

int ihk_mc_get_numa_distance(int i, int j)
{
	int *distance;

	if (i < 0 || i >= boot_param->nr_numa_nodes ||
		j < 0 || j >= boot_param->nr_numa_nodes) {
		return -1;
	}

	distance = (int *)((char *)boot_param + sizeof(*boot_param) +
			 boot_param->nr_cpus *
				sizeof(struct ihk_smp_boot_param_cpu) +
			 boot_param->nr_numa_nodes *
				sizeof(struct ihk_smp_boot_param_numa_node) +
			 boot_param->nr_memory_chunks *
				sizeof(struct ihk_smp_boot_param_memory_chunk));
	distance += (i * boot_param->nr_numa_nodes + j);

	return *distance;
}

int ihk_mc_get_nr_memory_chunks(void)
{
	return boot_param->nr_memory_chunks;
}

int ihk_mc_get_memory_chunk(int id,
		unsigned long *start,
		unsigned long *end,
		int *numa_id)
{
	struct ihk_smp_boot_param_memory_chunk *chunk;

	if (id < 0 || id >= boot_param->nr_memory_chunks)
		return -1;

	chunk = ((struct ihk_smp_boot_param_memory_chunk *)
			((char *)boot_param + sizeof(*boot_param) +
			 boot_param->nr_cpus * sizeof(struct ihk_smp_boot_param_cpu) +
			 boot_param->nr_numa_nodes *
			 sizeof(struct ihk_smp_boot_param_numa_node))) + id;

	if (start) *start = chunk->start;
	if (end) *end = chunk->end;
	if (numa_id) *numa_id = chunk->numa_id;

	return 0;
}

int ihk_mc_get_nr_cores(void)
{
	return boot_param->nr_cpus;
}

int ihk_mc_get_nr_linux_cores(void)
{
	return boot_param->nr_linux_cpus;
}

int ihk_mc_get_core(int id, unsigned long *linux_core_id, unsigned long *apic_id, int *numa_id)
{
	if (id < 0 || id >= boot_param->nr_cpus)
		return -1;

	if(linux_core_id) *linux_core_id = ihk_cpu_info->linux_cpu_ids[id];
	if(apic_id) *apic_id = ihk_cpu_info->hw_ids[id];
	if(numa_id) *numa_id = ihk_cpu_info->nodes[id];

	return 0;
}

int ihk_mc_get_ikc_cpu(int id)
{
	if (id < 0 || id >= boot_param->nr_cpus || ihk_cpu_info == NULL)
		return -1;

	return ihk_cpu_info->ikc_cpus[id];
}

int ihk_mc_get_apicid(int linux_core_id) {
	return boot_param->ihk_ikc_irq_apicids[linux_core_id];
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

void x86_set_warm_reset(unsigned long ip, char *first_page_va)
{
	/* Write CMOS */
	asm volatile("outb %0, $0x70" : : "a"((char)0xf));
	asm volatile("outb %0, $0x71" : : "a"((char)0xa));

	/* Set vector */
	*(unsigned short *)(first_page_va + 0x469) = (ip >> 4);
	*(unsigned short *)(first_page_va + 0x467) = ip & 0xf;
}

void builtin_mc_dma_init(unsigned long cfg_addr);

void ihk_mc_dma_init(void)
{
	builtin_mc_dma_init(boot_param->dma_address);
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
