/* setup.c COPYRIGHT FUJITSU LIMITED 2015-2019 */
#include <ihk/debug.h>
#include <ihk/mm.h>
#include <ihk/cpu.h>
#include <ihk/perfctr.h>
#include <errno.h>
#include <registers.h>
#include <arch-memory.h>
#include <arch-perfctr.h>
#include <arch/cpu.h>
#include <irq.h>
#include "bootparam.h"
#include <kmsg.h>
#include <llist.h>
#include <kmalloc.h>
#include <arch-perfctr.h>

/* BUILTIN Setup.c */
unsigned long boot_param_pa;
struct smp_boot_param *boot_param;
int boot_param_size;

unsigned long bootstrap_mem_end;

extern int num_processors;
extern void main(void);
extern void setup_arm64(void);
extern struct ihk_kmsg_buf *kmsg_buf;

unsigned long ap_trampoline = 0;
unsigned long arm64_kernel_phys_base = 0;
unsigned long arm64_st_phys_base = 0;
unsigned long arm64_st_phys_size = 0;

#ifndef IHK_IKC_USE_LINUX_WORK_IRQ
int spi_table[SMP_MAX_CPUS];
int nr_spi_table = 0;
#endif // !IHK_IKC_USE_LINUX_WORK_IRQ

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

int ihk_mc_linux_cpu_2_mckernel_cpu(int cpu)
{
	if (cpu < 0 || cpu >= ihk_cpu_info->nlinux_cpus)
		return -1;

	return ihk_cpu_info->mckernel_cpu_ids[cpu];
}

int ihk_mc_mckernel_cpu_2_linux_cpu(int cpu)
{
	if (cpu < 0 || cpu >= ihk_cpu_info->ncpus)
		return -1;

	return ihk_cpu_info->linux_cpu_ids[cpu];
}

static void build_ihk_cpu_info(void)
{
	int i;
	struct ihk_smp_boot_param_cpu *bp_cpu;

	ihk_cpu_info = early_alloc_pages((
				(sizeof(*ihk_cpu_info) + boot_param->nr_cpus *
				 (sizeof(ihk_cpu_info->hw_ids) +
				  sizeof(ihk_cpu_info->nodes) +
				  sizeof(ihk_cpu_info->linux_cpu_ids) +
				  sizeof(ihk_cpu_info->ikc_cpus)) +
				 (sizeof(ihk_cpu_info->mckernel_cpu_ids) *
					boot_param->nr_linux_cpus) +
				 PAGE_SIZE - 1) >> PAGE_SHIFT));
	ihk_cpu_info->hw_ids = (int *)(ihk_cpu_info + 1);
	ihk_cpu_info->nodes = (int *)(ihk_cpu_info + 1) +
		boot_param->nr_cpus;
	ihk_cpu_info->linux_cpu_ids = (int *)(ihk_cpu_info->nodes) +
		boot_param->nr_cpus;
	ihk_cpu_info->ikc_cpus = (int *)(ihk_cpu_info->linux_cpu_ids) +
		boot_param->nr_cpus;
	ihk_cpu_info->mckernel_cpu_ids = (int *)(ihk_cpu_info->ikc_cpus) +
		boot_param->nr_cpus;

	bp_cpu = (struct ihk_smp_boot_param_cpu *)(boot_param + 1);
	for (i = 0; i < boot_param->nr_linux_cpus; ++i) {
		ihk_cpu_info->mckernel_cpu_ids[i] = -1;
	}

	for (i = 0; i < boot_param->nr_cpus; ++i) {
		ihk_cpu_info->hw_ids[i] = bp_cpu->hw_id;
		ihk_cpu_info->nodes[i] = bp_cpu->numa_id;
		ihk_cpu_info->linux_cpu_ids[i] = bp_cpu->linux_cpu_id;
		ihk_cpu_info->ikc_cpus[i] = bp_cpu->ikc_cpu;
		ihk_cpu_info->mckernel_cpu_ids[bp_cpu->linux_cpu_id] = i;
		++bp_cpu;
	}

	ihk_cpu_info->ncpus = boot_param->nr_cpus;
	ihk_cpu_info->nlinux_cpus = boot_param->nr_linux_cpus;

#ifdef IHK_IKC_USE_LINUX_WORK_IRQ
	/* Map Linux IRQ work raised_list list heads */
	for (i = 0; i < boot_param->nr_linux_cpus; ++i) {
		uint64_t phys = (uint64_t)boot_param->ihk_ikc_cpu_raised_list[i];

		boot_param->ihk_ikc_cpu_raised_list[i] =
			map_fixed_area(phys, PAGE_SIZE, 0);
		if (!boot_param->ihk_ikc_cpu_raised_list[i]) {
			kprintf("error: mapping Linux IRQ raised list head\n");
			panic("");
		}
		dkprintf("%s: CPU %d, raised_list: 0x%lx -> 0x%lx\n",
			__func__, i, boot_param->ihk_ikc_cpu_raised_list[i], phys);
	}
#endif // IHK_IKC_USE_LINUX_WORK_IRQ
}

#ifndef IHK_IKC_USE_LINUX_WORK_IRQ
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
#endif // !IHK_IKC_USE_LINUX_WORK_IRQ

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

	/* Map boot parameter structure with the non-bootstrap map */
	boot_param = map_fixed_area(boot_param_pa, boot_param_size, 0);

	/* Map kmsg_buf, which is out of kernel image, with the
	 * non-bootstrap map. */
	ihk_get_kmsg_buf(&msg_buffer, &msg_buffer_size);
	kmsg_buf = (struct ihk_kmsg_buf *)
		map_fixed_area(msg_buffer, msg_buffer_size, 0);
	kmsg_init();
	kprintf("boot_param_size: %lu\n", boot_param_size);

	build_ihk_cpu_info();
#ifndef IHK_IKC_USE_LINUX_WORK_IRQ
	build_spi_table();
#endif // !IHK_IKC_USE_LINUX_WORK_IRQ

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

#ifdef IHK_IKC_USE_LINUX_WORK_IRQ
extern void (*arm64_issue_host_ipi)(int, int);
int ihk_mc_ikc_arch_issue_host_ipi(int cpu, int vector)
{
	arm64_issue_host_ipi(cpu, vector);
	return 0;
}
#else
static int ihk_mc_get_irq(int linux_core_id)
{
	return spi_table[linux_core_id];
}

extern void (*arm64_issue_ipi)(int, int);
int ihk_mc_interrupt_host(int cpu, int vector)
{
	arm64_issue_ipi(ihk_mc_get_apicid(cpu), ihk_mc_get_irq(cpu));
	return 0;
}
#endif // IHK_IKC_USE_LINUX_WORK_IRQ

int ihk_mc_get_vector(enum ihk_mc_gv_type type)
{
	switch (type) {
	case IHK_GV_IKC:
		return INTRID_IKC;
	case IHK_GV_QUERY_FREE_MEM:
		return INTRID_QUERY_FREE_MEM;
	default:
		if ((type >= IHK_TLB_FLUSH_IRQ_VECTOR_START) &&
				(type < IHK_TLB_FLUSH_IRQ_VECTOR_END)) {
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

void ihk_mc_get_boot_time(unsigned long *tv_sec, unsigned long *tv_nsec,
			  unsigned long *tsc)
{
	*tv_sec = boot_param->boot_sec;
	*tv_nsec = boot_param->boot_nsec;
	*tsc = boot_param->boot_tsc;
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

int ihk_set_multi_intr_mode_addr(unsigned long addr)
{
	boot_param->multi_intr_mode_addr = addr;

	return 0;
}

int ihk_set_nmi_mode_addr(unsigned long addr)
{
	boot_param->nmi_mode_addr = addr;
	
	return 0;
}

int ihk_set_mckernel_do_futex(unsigned long addr)
{
	boot_param->mckernel_do_futex = addr; /* Pass virtual address */
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

int ihk_mc_get_topology_view(void)
{
	return boot_param->topology_view;
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

int ihk_mc_get_linux_default_huge_page_shift(void)
{
	return boot_param->linux_default_huge_page_shift;
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

unsigned long ihk_mc_hw_event_map(unsigned long hw_event)
{
	const struct arm_pmu *pmu = get_cpu_pmu();
	int val = pmu->map_hw_event(hw_event);

	return (val < 0) ? ULONG_MAX : val;
}

unsigned long ihk_mc_hw_cache_event_map(unsigned long hw_cache_event)
{
	const struct arm_pmu *pmu = get_cpu_pmu();
	int val = pmu->map_cache_event(hw_cache_event);

	return (val < 0) ? ULONG_MAX : val;
}

unsigned long ihk_mc_raw_event_map(unsigned long  raw_event)
{
	const struct arm_pmu *pmu = get_cpu_pmu();
	int val = pmu->map_raw_event(raw_event);

	return (val < 0) ? ULONG_MAX : val;
}

int ihk_mc_validate_event(unsigned long hw_config)
{
	return (hw_config != ULONG_MAX);
}
#else /* ENABLE_PERF */
unsigned long ihk_mc_hw_event_map(unsigned long  hw_event)
{
	return get_cpu_pmu() ?
		get_cpu_pmu()->map_event(PERF_TYPE_HARDWARE, hw_event) : 0;
}

unsigned long ihk_mc_hw_cache_event_map(unsigned long hw_cache_event)
{
	return get_cpu_pmu() ?
		get_cpu_pmu()->map_event(PERF_TYPE_HW_CACHE, hw_cache_event) : 0;
}

unsigned long ihk_mc_raw_event_map(unsigned long  raw_event)
{
	return 0;
}

int ihk_mc_validate_event(unsigned long hw_config)
{
	return 0;
}
#endif /* ENABLE_PERF */

int ihk_mc_get_extra_reg_id(unsigned long hw_config, unsigned long hw_config_ext)
{
	/* Nothing to do. */
	return -1;
}

int ihk_mc_get_extra_reg_idx(int id)
{
	/* Nothing to do. */
	return 0;
}

unsigned int ihk_mc_get_extra_reg_msr(int id)
{
	/* Nothing to do. */
	return 0;
}

unsigned long ihk_mc_get_extra_reg_event(int id)
{
	/* Nothing to do. */
	return 0;
}

unsigned long ihk_mc_hw_cache_extra_reg_map(unsigned long hw_cache_event)
{
	/* Nothing to do. */
	return 0;
}
