/* setup.c COPYRIGHT FUJITSU LIMITED 2015-2017 */
#include <ihk/debug.h>
#include <ihk/mm.h>
#include <ihk/cpu.h>
#include <ihk/perfctr.h>
#include <errno.h>
#include <registers.h>
#include <arch-memory.h>
#include <arch/cpu.h>
#include <irq.h>
#include "bootparam.h"
#include <kmsg.h>

/* BUILTIN Setup.c */
unsigned long boot_param_pa;
struct smp_boot_param *boot_param;
int boot_param_size;

unsigned long bootstrap_mem_end;

extern void main(void);
extern void setup_arm64(void);
extern struct ihk_kmsg_buf *kmsg_buf;

unsigned long ap_trampoline = 0;
unsigned long arm64_kernel_phys_base = 0;
unsigned long arm64_st_phys_base = 0;
unsigned long arm64_st_phys_size = 0;

int spi_table[SMP_MAX_CPUS];
int nr_spi_table = 0;

struct ihk_dump_page * dump_page;

struct start_kernel_param {
	unsigned long param_addr;
	unsigned long phys_address;
	unsigned long st_phys_base;
	unsigned long st_phys_size;
	unsigned long gic_dist_base_pa;
	unsigned long gic_cpu_base_pa;
};

/* NOTEs on parameters: 
 *
 * param_addr (RDI) is set in shimos_trampoline_64.S before jumping
 * into starrtup.S 
 * phys_addr (RSI), ap_trampoline_start (RDX) are set in startup.S
 */

/* get paramater into:
 *   param_addr:         struct ihk_smp_trampoline_header::notify_address
 *   phys_address:       startup[4] (phys)
 */
void start_kernel(struct start_kernel_param *param)
{
	arm64_kernel_phys_base = param->phys_address;
	arm64_st_phys_base = param->st_phys_base;
	arm64_st_phys_size = param->st_phys_size;
	boot_param_pa = param->param_addr;
#ifdef CONFIG_ARM64_64K_PAGES
	/* head.S: BLOCK_SIZE == PAGE_SIZE */
	boot_param = (void*)(MAP_BOOT_PARAM + page_offset(boot_param_pa));
#else
	/* head.S: BLOCK_SIZE == LARGE_PAGE_SIZE */
	boot_param = (void*)(MAP_BOOT_PARAM + large_page_offset(boot_param_pa));
#endif /*CONFIG_ARM64_64K_PAGES*/
	bootstrap_mem_end = boot_param->bootstrap_mem_end;
	boot_param_size = boot_param->param_size;

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

static void build_spi_table(void)
{
	int checkers[SMP_MAX_CPUS];
	int ikc_cpus[SMP_MAX_IRQS] = { 0 };
	int i, j, k = 0;
	int max = ihk_cpu_info->ikc_cpus[0];
	int min = ihk_cpu_info->ikc_cpus[0];

	/* initialize spi_table */
	for (i = 0; i < SMP_MAX_CPUS; i++) {
		spi_table[i] = -1;
	}

	/* initialize check table */
	if (SMP_MAX_CPUS < ihk_cpu_info->ncpus) {
		panic("ihk_cpu_info->ncpus over SMP_MAX_CPUS.");
	}

	for (i = 0; i < ihk_cpu_info->ncpus; i++) {
		checkers[i] = 0;
	}

	/* pickup same value in ihk_cpu_info->ikc_cpus[i] */
	for (i = 0; i < ihk_cpu_info->ncpus; i++) {
		if (checkers[i] == 1) {
			continue;
		}

		if (max < ihk_cpu_info->ikc_cpus[i]) {
			max = ihk_cpu_info->ikc_cpus[i];
		}

		if (ihk_cpu_info->ikc_cpus[i] < min) {
			min = ihk_cpu_info->ikc_cpus[i];
		}

		for (j = 0; j < ihk_cpu_info->ncpus; j++) {
			if (ihk_cpu_info->ikc_cpus[i] == ihk_cpu_info->ikc_cpus[j]) {
				checkers[j] = 1;
			}
		}

		if (k < SMP_MAX_IRQS) {
			ikc_cpus[k] = ihk_cpu_info->ikc_cpus[i];
			k++;
		} else {
			panic("ikc_map pattern over SMP_MAX_IRQS.");
		}
	}
	nr_spi_table = ++max;

	/* set spi_table */
	/* must HOST-Linux#0 core setting */
	if (min != 0) {
		if (boot_param->ihk_ikc_irqs[0] != -1) {
			spi_table[0] = boot_param->ihk_ikc_irqs[0];
		} else {
			panic("ihk_ikc_irqs for HOST-Linux#0 core is empty.");
		}
		j = 1;
	} else {
		j = 0;
	}

	for (i = 0; i < k; i++, j++) {
		if (boot_param->ihk_ikc_irqs[j] != -1) {
			spi_table[ikc_cpus[i]] = boot_param->ihk_ikc_irqs[j];
		} else {
			panic("ikc_map pattern over ihk_nr_irqs.");
		}
	}
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

extern void init_page_table(void);
void arch_init(void)
{
	unsigned long msg_buffer, msg_buffer_size;
	struct smp_boot_param *initial_boot_param;
	size_t pgsize;
	struct page_table *pt;
	int (*clear_page)(page_table_t, void*) = NULL;

	extern char _head[], _end[];
	if (((unsigned long)_head != MAP_KERNEL_START) || 
		((unsigned long)_end + MAP_EARLY_ALLOC_SIZE + MAP_BOOT_PARAM_SIZE < (unsigned long)_end)) {
		panic("kernel image too large.");
	}

	/* Ack boot (trampoline code shall be free'd) */
	boot_param->status = 1;
	initial_boot_param = boot_param;

	init_page_table();

	kprintf("boot_param_size: %lu\n", boot_param_size);

	/* Map boot parameter structure with the non-bootstrap map */
	boot_param = map_fixed_area(boot_param_pa, boot_param_size, 0);
	build_ihk_cpu_info();
	build_spi_table();

	setup_arm64();

	pt = get_init_page_table();
	ihk_mc_pt_lookup_pte(pt, initial_boot_param, 0, NULL, &pgsize, NULL);
	if (pgsize == PAGE_SIZE) {
		clear_page = ihk_mc_pt_clear_page;
	} else if (pgsize == LARGE_PAGE_SIZE) {
		clear_page = ihk_mc_pt_clear_large_page;
	}
	if (clear_page) {
		unsigned long addr = (unsigned long)initial_boot_param;
		while (addr < MAP_BOOT_PARAM_END) {
			clear_page(pt, (void*)addr);
			addr += pgsize;
		}
	}

	dump_page = (struct ihk_dump_page *)map_fixed_area(boot_param->dump_page_set.phy_page, boot_param->dump_page_set.page_size, 0);

	/* Map kmsg_buf, which is out of kernel image, with the non-bootstrap map. */
	ihk_get_kmsg_buf(&msg_buffer, &msg_buffer_size);
	kmsg_buf = (struct ihk_kmsg_buf *)map_fixed_area(msg_buffer, msg_buffer_size, 0);
	kmsg_init();
	kputs("IHK/McKernel started.\n");

	kprintf("ns_per_tsc: %lu\n", boot_param->ns_per_tsc);
}

void arch_ready(void)
{
	/* Make it ready */
	boot_param->status = 2;
	barrier();
}

void done_init(void)
{
	/* Make it running */
	boot_param->status = 3;
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
	default:
		break;
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

static int ihk_mc_get_irq(int linux_core_id)
{
	return spi_table[linux_core_id];
}

extern void (*arm64_issue_ipi)(int, int);
int ihk_mc_interrupt_host(int cpu, int vector)
{
	(*arm64_issue_ipi)(ihk_mc_get_apicid(cpu), ihk_mc_get_irq(cpu));
	return 0;
}

int ihk_mc_get_vector(enum ihk_mc_gv_type type)
{
	switch (type) {
	case IHK_GV_IKC:
		return INTRID_IKC;
	case IHK_GV_QUERY_FREE_MEM:
		return INTRID_QUERY_FREE_MEM;
	default:
		if((type >= IHK_TLB_FLUSH_IRQ_VECTOR_START) && 
		   (type < IHK_TLB_FLUSH_IRQ_VECTOR_END)){
			return INTRID_TLB_FLUSH;
		}
	}
	return -ENOENT;
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

int ihk_get_kmsg_buf(unsigned long *addr, unsigned long *size)
{
	*addr = boot_param->msg_buffer;
	*size = boot_param->msg_buffer_size;
	return 0;
}

int ihk_set_monitor(unsigned long addr, unsigned long size)
{
	boot_param->monitor = addr;
	boot_param->monitor_size = size;
	
	return 0;
}

int ihk_set_rusage(unsigned long addr, unsigned long size)
{
	boot_param->rusage = addr;
	boot_param->rusage_size = size;
	
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

int ihk_mc_get_osnum(void)
{
	return boot_param->osnum;
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
	return boot_param->ihk_ikc_cpu_hwids[linux_core_id];
}

/* @ref.impl linux-linaro/init/main.c::loops_per_jiffy, get from partitioning module */
extern unsigned long ihk_param_lpj;

/* @ref.impl linux-linaro/include/asm-generic/param.h::HZ, get from partitioning module */
extern unsigned long ihk_param_hz;

/* @ref.impl linux-linaro/arch/arm64/lib/delay.c::__udelay, __const_udelay, __delay */
void arch_delay(int us)
{
	unsigned long xloops = (unsigned long)us * 0x10C7UL;	/* 2**32 / 1000000 (rounded up) */
	unsigned long loops = xloops * ihk_param_lpj * ihk_param_hz;
	unsigned long cycles = loops >> 32UL;
	unsigned long start = rdtsc();

	while ((rdtsc() - start) < cycles)
		cpu_pause();
}

void arm64_set_warm_reset(unsigned long ip, char *first_page_va)
{
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

unsigned int *arm64_march_perfmap = perf_map_nehalem;

void ihk_mc_set_dump_level(unsigned int level)
{
	boot_param->dump_level = level;
	return;
}

unsigned int ihk_mc_get_dump_level(void)
{
	return (boot_param->dump_level);
}

struct ihk_dump_page_set *ihk_mc_get_dump_page_set(void)
{
	return (&boot_param->dump_page_set);
}

struct ihk_dump_page *ihk_mc_get_dump_page(void)
{
	return (dump_page);
}

#ifdef ENABLE_PERF
int ihk_mc_get_extra_reg_id(unsigned long hw_config, unsigned long hw_config_ext)
{
	return 0;
}

int ihk_mc_get_extra_reg_idx(int id)
{
	return 0;
}

unsigned int ihk_mc_get_extra_reg_msr(int id)
{
	return 0;
}

unsigned long ihk_mc_get_extra_reg_event(int id)
{
	return 0;
}

unsigned long ihk_mc_hw_event_map(unsigned long  hw_event)
{
	return 0;
}

unsigned long ihk_mc_hw_cache_event_map(unsigned long hw_cache_event)
{
	return 0;
}

unsigned long ihk_mc_hw_cache_extra_reg_map(unsigned long hw_cache_event)
{
	return 0;
}
#else /* ENABLE_PERF */
int ihk_mc_get_extra_reg_id(unsigned long hw_config, unsigned long hw_config_ext)
{
	return 0;
}

int ihk_mc_get_extra_reg_idx(int id)
{
	return 0;
}

unsigned int ihk_mc_get_extra_reg_msr(int id)
{
	return 0;
}

unsigned long ihk_mc_get_extra_reg_event(int id)
{
	return 0;
}

unsigned long ihk_mc_hw_event_map(unsigned long  hw_event)
{
	return 0;
}

unsigned long ihk_mc_hw_cache_event_map(unsigned long hw_cache_event)
{
	return 0;
}

unsigned long ihk_mc_hw_cache_extra_reg_map(unsigned long hw_cache_event)
{
	return 0;
}
#endif /* ENABLE_PERF */
