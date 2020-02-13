/* smp-arch-driver.c COPYRIGHT FUJITSU LIMITED 2015-2019 */
/**
 * \file smp-arch-driver.c
 * \brief
 *	IHK SMP-ARM64 Driver: IHK Host Driver
 *                          for partitioning an AARCH64 SMP chip
 */
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/list.h>
#ifdef IHK_IKC_USE_LINUX_WORK_IRQ
#include <linux/irq_work.h>
#endif // IHK_IKC_USE_LINUX_WORK_IRQ
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/of_address.h>
#include <linux/acpi.h>
#include <linux/version.h>
#include <linux/kallsyms.h>
#include <linux/platform_device.h>
#include <linux/perf_event.h>
#include <linux/irqchip/arm-gic-v3.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,4,0)
# include <asm/pmu.h>
#else
# include <linux/perf/arm_pmu.h>
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,12,0)
#include <asm/uaccess.h>
#else
#include <linux/uaccess.h>
#endif
#include <linux/psci.h>
#include <linux/fs.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,12,0)
#include <linux/kallsyms.h>
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(4,12,0) */
#include <uapi/linux/psci.h>
#include <ihk/misc/debug.h>
#include <ihk/ihk_host_user.h>
#include <dt-bindings/interrupt-controller/arm-gic.h>
#include "config.h"
#include "smp-driver.h"
#include "smp-arch-driver.h"
#include "smp-defines-driver.h"
#include "host_linux.h"

/* Allocate monitor */
void setup_monitor(struct ihk_host_linux_os_data *data);

/* ----------------------------------------------- */

static unsigned int ihk_start_irq = 0;
module_param(ihk_start_irq, uint, 0644);
MODULE_PARM_DESC(ihk_start_irq, "IHK IKC IPI to be scanned from this IRQ vector");

static unsigned long ihk_trampoline = 0;
module_param(ihk_trampoline, ulong, 0644);
MODULE_PARM_DESC(ihk_trampoline, "IHK trampoline page physical address");

static unsigned int ihk_nr_irq = 1;
module_param(ihk_nr_irq, uint, 0444);
MODULE_PARM_DESC(ihk_nr_irq, "Number of IHK IKC vector");

#define D(fmt, ...) \
        printk( "%s(%d) " fmt, __func__, __LINE__, ## __VA_ARGS__ )

static struct page *trampoline_page;
static void *trampoline_va;

static int ident_npages_order = 0;
static unsigned long *ident_page_table_virt;

#ifdef IHK_IKC_USE_LINUX_WORK_IRQ
static int ihk_smp_irq = -1;
#else
struct ihk_smp_irq_table {
	int irq;
	int hwirq;
	char irq_name[16];
};
static struct ihk_smp_irq_table ihk_smp_irq[SMP_MAX_IRQS];
#endif // IHK_IKC_USE_LINUX_WORK_IRQ

extern const char ihk_smp_trampoline_end[], ihk_smp_trampoline_data[];
#define IHK_SMP_TRAMPOLINE_SIZE ((unsigned long)(ihk_smp_trampoline_end - ihk_smp_trampoline_data))

static phys_addr_t ihk_smp_gic_dist_base_pa = 0;
static unsigned long ihk_smp_gic_dist_size = 0;
static phys_addr_t ihk_smp_gic_cpu_base_pa = 0;
static unsigned long ihk_smp_gic_cpu_size = 0;
static unsigned int ihk_gic_percpu_offset = 0;
static phys_addr_t ihk_smp_gic_rdist_pa[NR_CPUS];

/*
 * If you edit IPI_XXX, you must edit following together.
 * mckernel/arch/arm64/kernel/include/irq.h::INTRID_xxx
 */
#define INTRID_CPU_STOP	3
#define INTRID_MULTI_INTR	6
#define INTRID_MULTI_NMI	7

/* ----------------------------------------------- */

/**
 * @ref.impl drivers/irqchip/irq-gic-v3.c:struct redist_region
 */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,19,0))
#error "No defined struct redist_region for less than linux3.19"
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(4,6,0))
/* for linux3.19 - linux4.5 */
struct redist_region {
	void __iomem	*redist_base;
	phys_addr_t	phys_base;
};
#else
/* for linux4.6 - */
struct redist_region {
	void __iomem		*redist_base;
	phys_addr_t		phys_base;
	bool			single_redist;
};
#endif

/**
 * @ref.impl drivers/irqchip/irq-gic-v3.c:struct gic_chip_data
 */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,19,0))
#error "No defined struct gic_chip_data_v3 for less than linux3.19"
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(4,7,0))
/* for linux3.19 - linux4.6 */
struct gic_chip_data_v3 {
	void __iomem		*dist_base;
	struct redist_region	*redist_regions;
	struct rdists		rdists;
	struct irq_domain	*domain;
	u64			redist_stride;
	u32			nr_redist_regions;
	unsigned int		irq_nr;
};
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(4,15,0))
/* for linux4.7 - linux4.14 */
struct gic_chip_data_v3 {
	struct fwnode_handle	*fwnode;
	void __iomem		*dist_base;
	struct redist_region	*redist_regions;
	struct rdists		rdists;
	struct irq_domain	*domain;
	u64			redist_stride;
	u32			nr_redist_regions;
#if defined(RHEL_RELEASE_CODE) && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7, 5)
	bool			has_rss;
#endif
	unsigned int		irq_nr;
	struct partition_desc	*ppi_descs[16];
};
#else
/* for linux4.15 - */
struct gic_chip_data_v3 {
	struct fwnode_handle	*fwnode;
	void __iomem		*dist_base;
	struct redist_region	*redist_regions;
	struct rdists		rdists;
	struct irq_domain	*domain;
	u64			redist_stride;
	u32			nr_redist_regions;
	bool			has_rss;
	unsigned int		irq_nr;
	struct partition_desc	*ppi_descs[16];
};
#endif
static unsigned long ihk_gic_version = ACPI_MADT_GIC_VERSION_NONE;
static unsigned int ihk_gic_max_vector = 0;

/**
 * @ref.impl arch/arm64/kernel/psci.c:struct psci_operations
 */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,9,0))
#error "No defined struct psci_operations for less than linux3.9"
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(4,2,0))
/* for linux3.9 - linux4.1 */
struct psci_power_state {
	u16	id;
	u8	type;
	u8	affinity_level;
};

struct psci_operations {
	int (*cpu_suspend)(struct psci_power_state state,
	                   unsigned long entry_point);
	int (*cpu_off)(struct psci_power_state state);
	int (*cpu_on)(unsigned long cpuid, unsigned long entry_point);
	int (*migrate)(unsigned long cpuid);
	int (*affinity_info)(unsigned long target_affinity,
	                     unsigned long lowest_affinity_level);
	int (*migrate_info_type)(void);
};
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(4,3,0))
/* for linux4.2 */
struct psci_operations {
	int (*cpu_suspend)(u32 state, unsigned long entry_point);
	int (*cpu_off)(u32 state);
	int (*cpu_on)(unsigned long cpuid, unsigned long entry_point);
	int (*migrate)(unsigned long cpuid);
	int (*affinity_info)(unsigned long target_affinity,
			unsigned long lowest_affinity_level);
	int (*migrate_info_type)(void);
};
/* for linux4.3 - defined move to include/linux/psci.h */
#endif

struct ihk_smp_trampoline_header {
	unsigned long reserved;	/* jmp ins. */
	unsigned long page_table;	/* ident page table */
	unsigned long next_ip;	/* the program address */
	unsigned long stack_ptr;	/* stack pointer */
	unsigned long notify_address;	/* notification address */
	unsigned long startup_data;	/* startup_data addr */
	unsigned long st_phys_base;	/* straight map phys base address */
	unsigned long st_phys_size;	/* straight map area size */
	unsigned long dist_base_pa;	/* GIC distributor register base addr */
	unsigned long dist_map_size;	/* GIC distributor register map size */
	unsigned long cpu_base_pa;	/* GIC cpu interface register base addr */
	unsigned long cpu_map_size;	/* GIC cpu interface register map size */
	unsigned int  percpu_offset;	/* GIC cpu interface register map offset value */
	unsigned int  gic_version;	/* GIC version value */
	unsigned long loops_per_jiffy;	/* udelay loops value */
	unsigned long hz;		/* HZ value */
#define PSCI_METHOD_INVALID	-1
#define PSCI_METHOD_HVC		0
#define PSCI_METHOD_SMC		1
	unsigned long psci_method;	/* psci_method value (smc or hvc ?) */
	unsigned long use_virt_timer;	/* use_virt_value */
	unsigned long evtstrm_timer_rate;	/* arch_timer_rate */
	unsigned long default_vl;		/* SVE default VL */
	unsigned long cpu_logical_map_size;	/* the cpu-core maximun number */
	unsigned long cpu_logical_map[NR_CPUS];	/* array of the MPIDR and the core number */
	unsigned long rdist_base_pa[NR_CPUS];	/* GIC re-distributor register base addresses */
	unsigned long retention_state_flag_pa;
	int nr_pmu_irq_affi;		/* number of pmu affinity list elements */
	int pmu_irq_affi[SMP_MAX_CPUS];	/* array of the pmu affinity list */
};

static unsigned long ihk_smp_psci_method = PSCI_METHOD_INVALID;	/* psci_method value */

/* ----------------------------------------------- */

/*
 * IHK-SMP unexported kernel symbols
 */
static struct gic_chip_data_v3 *ihk_gic_data_v3;
static void (*ihk___smp_cross_call)(const struct cpumask *, unsigned int);


#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0)
static int (*ihk___irq_domain_alloc_irqs)(struct irq_domain *domain,
					  int irq_base, unsigned int nr_irqs,
					  int node, void *arg, bool realloc);
static int (*ihk_irq_domain_free_irqs)(unsigned int virq,
				       unsigned int nr_irqs);
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(4,4,0) */
static struct psci_operations *ihk_psci_ops;

static size_t ihk___cpu_logical_map_size = NR_CPUS;
static u64 *ihk___cpu_logical_map;
static uint64_t *ihk___memstart_addr;

static uintptr_t *ihk_invoke_psci_fn;
static uintptr_t ihk___invoke_psci_fn_hvc;
static uintptr_t ihk___invoke_psci_fn_smc;

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0)
static struct arm_pmu **ihk_cpu_pmu;
#else
static struct arm_pmu **ihk_cpu_pmu;
#endif

int (*ihk___irq_set_affinity)(unsigned int irq, const struct cpumask *mask,
			      bool force);

enum ppi_nr {
	PHYS_SECURE_PPI,
	PHYS_NONSECURE_PPI,
	VIRT_PPI,
	HYP_PPI,
	MAX_TIMER_PPI
};

enum ppi_nr *ihk_arch_timer_uses_ppi;

u32 *ihk_arch_timer_rate;

void (*ihk___flush_dcache_area)(void *addr, size_t len);
#ifdef IHK_IKC_USE_LINUX_WORK_IRQ
static char **ihk__ipi_types;
#endif // IHK_IKC_USE_LINUX_WORK_IRQ

/* There are two symbols with the same name, but thanksfully only one is
 * actually used, and the other will contain 0s by definition.
 * We can use that to figure which symbol to use
 */
int lookup_gic_data_v3(void *data, const char *name, struct module *mod,
		       unsigned long address)
{
	unsigned long *gic_data;
	int i;

	if (strcmp(name, "gic_data") == 0) {
		gic_data = (void *)address;
		/* check the first (arbitrary) 8 words for data */
		for (i = 0; i < 8; i++) {
			if (gic_data[i]) {
				ihk_gic_data_v3 = (void *) address;
				return 1;
			}
		}
	}

	return 0;
}

int ihk_smp_arch_symbols_init(void)
{
	void **ihk___smp_cross_call_p = NULL;

	if (WARN_ON(!kallsyms_on_each_symbol(lookup_gic_data_v3, NULL)))
		return -EFAULT;

	ihk___smp_cross_call_p = (void *) kallsyms_lookup_name("__smp_cross_call");
	if (WARN_ON(!ihk___smp_cross_call_p))
		return -EFAULT;

	ihk___smp_cross_call = *ihk___smp_cross_call_p;
	if (WARN_ON(!ihk___smp_cross_call))
		return -EFAULT;

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0)
	ihk___irq_domain_alloc_irqs =
		(void *) kallsyms_lookup_name("__irq_domain_alloc_irqs");
	if (WARN_ON(!ihk___irq_domain_alloc_irqs))
		return -EFAULT;

	ihk_irq_domain_free_irqs =
		(void *) kallsyms_lookup_name("irq_domain_free_irqs");
	if (WARN_ON(!ihk_irq_domain_free_irqs))
		return -EFAULT;
#endif
	ihk_psci_ops = (void *) kallsyms_lookup_name("psci_ops");
	if (WARN_ON(!ihk_psci_ops))
		return -EFAULT;

	ihk___cpu_logical_map =
		(void *) kallsyms_lookup_name("__cpu_logical_map");
	if (WARN_ON(!ihk___cpu_logical_map))
		return -EFAULT;

	ihk_invoke_psci_fn = (void *) kallsyms_lookup_name("invoke_psci_fn");
	if (WARN_ON(!ihk_invoke_psci_fn))
		return -EFAULT;

	ihk___invoke_psci_fn_hvc =
		(uintptr_t) kallsyms_lookup_name("__invoke_psci_fn_hvc");
	if (WARN_ON(!ihk___invoke_psci_fn_hvc))
		return -EFAULT;

	ihk___invoke_psci_fn_smc =
		(uintptr_t) kallsyms_lookup_name("__invoke_psci_fn_smc");
	if (WARN_ON(!ihk___invoke_psci_fn_smc))
		return -EFAULT;

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0)
	ihk_cpu_pmu = (void *) kallsyms_lookup_name("cpu_pmu");
#else
	ihk_cpu_pmu = (void *) kallsyms_lookup_name("__oprofile_cpu_pmu");
#endif
	if (WARN_ON(!ihk_cpu_pmu))
		return -EFAULT;

	ihk___irq_set_affinity =
		(void *) kallsyms_lookup_name("__irq_set_affinity");
	if (WARN_ON(!ihk___irq_set_affinity))
		return -EFAULT;

	ihk_arch_timer_uses_ppi =
		(void *) kallsyms_lookup_name("arch_timer_uses_ppi");
	if (WARN_ON(!ihk_arch_timer_uses_ppi))
		return -EFAULT;

	ihk_arch_timer_rate =
		(void *) kallsyms_lookup_name("arch_timer_rate");
	if (WARN_ON(!ihk_arch_timer_rate))
		return -EFAULT;

	ihk___flush_dcache_area =
		(void *) kallsyms_lookup_name("__flush_dcache_area");
	if (WARN_ON(!ihk___flush_dcache_area))
		return -EFAULT;

#ifdef IHK_IKC_USE_LINUX_WORK_IRQ
	ihk__ipi_types =
		(char **) kallsyms_lookup_name("ipi_types");
	if (WARN_ON(!ihk__ipi_types))
		return -EFAULT;
#endif // IHK_IKC_USE_LINUX_WORK_IRQ

	ihk___memstart_addr =
		(uint64_t *) kallsyms_lookup_name("memstart_addr");
	if (WARN_ON(!ihk___memstart_addr))
		return -EFAULT;

	return 0;
}


static unsigned long is_arch_timer_use_virt(void)
{
	if (*ihk_arch_timer_uses_ppi == VIRT_PPI) {
		return 1UL;
	} else if (MAX_TIMER_PPI > *ihk_arch_timer_uses_ppi) {
		return 0UL;
	} else {
		return ULONG_MAX;
	}
}

void smp_ihk_arch_dcache_flush(void *addr, size_t len)
{
	ihk___flush_dcache_area(addr, len);
}

#if 0 // TODO[PMU]
/* @ref.impl arch/arm64/kernel/perf_event.c:armpmu_reserve_hardware */
static int
ihk_armpmu_set_irq_affi(const int irqs[], const struct smp_os_data *os)
{
	int hwid, virtid;

	virtid = 0;
	for (hwid = 0; hwid < SMP_MAX_CPUS; hwid++) {
		int irq;
		if (!(CORE_ISSET(hwid, os->cpu_hw_ids_map))) {
			continue;
		}
		
		irq  = irqs[virtid];

		/*
		 * If we have a single PMU interrupt that we can't shift,
		 * assume that we're running on a uniprocessor machine and
		 * continue. Otherwise, continue without this interrupt.
		 */
		if (ihk___irq_set_affinity(irq, cpumask_of(hwid), true)) {
			pr_warning("unable to set irq affinity (irq=%d, cpu=%u)\n",
				   irq, hwid);
		}
		virtid++;
	}
	return 0;
}
#endif

int ihk_smp_get_hw_id(int cpu)
{
	return cpu;
}

#ifndef ACPI_GICV3_DIST_MEM_SIZE
#define ACPI_GICV3_DIST_MEM_SIZE	SZ_64K
#endif
#ifndef ACPI_GICV3_CPU_IF_MEM_SIZE
#define ACPI_GICV3_CPU_IF_MEM_SIZE	SZ_64K
#endif

static void ihk_smp_gic_collect_rdist(void)
{
	int cpu;

	/* Collect redistributor base addresses for all possible cpus */
	for_each_cpu(cpu, cpu_possible_mask) {
		ihk_smp_gic_rdist_pa[cpu] =
			(per_cpu_ptr(ihk_gic_data_v3->rdists.rdist, cpu))->phys_base;
	}

	if(ihk_gic_data_v3->redist_stride) {
		ihk_smp_gic_cpu_size = ihk_gic_data_v3->redist_stride;
	} else {
		unsigned long typer =
			readq_relaxed(((this_cpu_ptr(ihk_gic_data_v3->rdists.rdist))->rd_base) + GICR_TYPER);
		if (typer & GICR_TYPER_VLPIS) {
			ihk_smp_gic_cpu_size = ACPI_GICV3_CPU_IF_MEM_SIZE * 4; /* RD + SGI + VLPI + reserved */
		} else {
			ihk_smp_gic_cpu_size = ACPI_GICV3_CPU_IF_MEM_SIZE * 2; /* RD + SGI */
		}
	}

}

#ifdef CONFIG_ACPI
/*
 * @ref.impl drivers/acpi/tables.c:__init acpi_parse_entries
 */
static int ihk_smp_acpi_parse_entries(char *id, unsigned long table_size,
		acpi_tbl_entry_handler handler,
		struct acpi_table_header *table_header,
		int entry_id, unsigned int max_entries)
{
	struct acpi_subtable_header *entry;
	int count = 0;
	unsigned long table_end;

	if (acpi_disabled)
		return -ENODEV;

	if (!id || !handler)
		return -EINVAL;

	if (!table_size)
		return -EINVAL;

	if (!table_header) {
		printk(KERN_WARNING "%4.4s not present\n", id);
		return -ENODEV;
	}

	table_end = (unsigned long)table_header + table_header->length;

	/* Parse all entries looking for a match. */

	entry = (struct acpi_subtable_header *)
	    ((unsigned long)table_header + table_size);

	while (((unsigned long)entry) + sizeof(struct acpi_subtable_header) <
	       table_end) {
		if (entry->type == entry_id
		    && (!max_entries || count < max_entries)) {
			if (handler(entry, table_end))
				return -EINVAL;

			count++;
		}

		/*
		 * If entry->length is 0, break from this loop to avoid
		 * infinite loop.
		 */
		if (entry->length == 0) {
			printk(KERN_ERR "[%4.4s:0x%02x] Invalid zero length\n", id, entry_id);
			return -EINVAL;
		}

		entry = (struct acpi_subtable_header *)
		    ((unsigned long)entry + entry->length);
	}

	if (max_entries && count > max_entries) {
		printk(KERN_WARNING "[%4.4s:0x%02x] ignored %i entries of %i found\n",
			id, entry_id, count - max_entries, count);
	}

	return count;
}

/*
 * Get distributer base PA and gic version.
 *
 * @ref.impl drivers/irqchip/irq-gic.c:
 *		__init gic_acpi_parse_madt_distributor
 *		static bool __init gic_validate_dist
 * @ref.impl drivers/irqchip/irq-gic-v3.c:
 *		__init gic_acpi_parse_madt_distributor
 *		static bool __init acpi_validate_gic_table
 */
static int ihk_smp_acpi_parse_madt_distributor(struct acpi_subtable_header *header,
				const unsigned long end)
{

	struct ihk_acpi_madt_generic_distributor {
		struct acpi_subtable_header header;
		u16 reserved;		/* reserved - must be zero */
		u32 gic_id;
		u64 base_address;
		u32 global_irq_base;
		u8 version;
		u8 reserved2[3];	/* reserved - must be zero */
	};
	struct ihk_acpi_madt_generic_distributor *dist;

	dist = (struct ihk_acpi_madt_generic_distributor *)header;

	if (BAD_MADT_ENTRY(dist, end))
		return -EINVAL;

	// set to global
	ihk_gic_version = dist->version;
	ihk_smp_gic_dist_base_pa = dist->base_address;
	if(ihk_gic_version >= ACPI_MADT_GIC_VERSION_V3) {
		ihk_smp_gic_dist_size = ACPI_GICV3_DIST_MEM_SIZE;
	} else {
		pr_err("Unsupported GICv2 or less\n");
		return -EINVAL;
	}

	return 0;
}

/*
 * @ref.impl arch/arm64/kernel/acpi.c:__init acpi_gic_init
 * @ref.impl drivers/irqchip/irq-gic.c:__init gic_v2_acpi_init
 * @ref.impl drivers/irqchip/irq-gic-v3.c:__init gic_v3_acpi_init
 */
static int ihk_smp_acpi_get_gic_base(void)
{
	struct acpi_table_header *table;
	acpi_status status;
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,10,0)
	acpi_size tbl_size;
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(4,10,0) */
	int count = 0;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,10,0)
	status = acpi_get_table(ACPI_SIG_MADT, 0, &table);
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(4,10,0) */
	status = acpi_get_table_with_size(ACPI_SIG_MADT, 0, &table, &tbl_size);
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(4,10,0) */
	if (ACPI_FAILURE(status)) {
		printk("ERROR: Failed to get MADT table.\n");
		return -ENODATA;
	}

	/*
	 * Find distributor base address. We expect one distributor entry since
	 * ACPI 5.1 spec neither support multi-GIC instances nor GIC cascade.
	 */
	count = ihk_smp_acpi_parse_entries(ACPI_SIG_MADT,
				   sizeof(struct acpi_table_madt),
				   ihk_smp_acpi_parse_madt_distributor, table,
				   ACPI_MADT_TYPE_GENERIC_DISTRIBUTOR, 0);
	if (count < 0) {
		pr_err("Error during GICD entries parsing\n");
		return -EINVAL;
	} else if (!count) {
		pr_err("No valid GICD entries exist\n");
		return -ENXIO;
	} else if (count > 1) {
		pr_err("More than one GICD entry detected\n");
		return -EINVAL;
	}

	// for GICv3 or later
	/*
	 * Collect re-distributor base PA information
	 * that the host-linux was constructed.
	 */
	ihk_smp_gic_collect_rdist();

	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,12,0)
#define GICC_FUNC_NAME	"acpi_cpu_get_madt_gicc"
typedef struct acpi_madt_generic_interrupt *(*gicc_func_t)(int cpu);
static int
ihk_armpmu_get_irq_affi_acpi(int irqs[], const struct arm_pmu *armpmu,
			     const struct smp_os_data *os)
{
	struct acpi_madt_generic_interrupt *gicc = NULL;
	int irq = 0, virtid = 0, hwid = 0;
	u32 gsi = 0;
	gicc_func_t gicc_func = NULL;

	gicc_func = (gicc_func_t)kallsyms_lookup_name(GICC_FUNC_NAME);
	if (!gicc_func) {
		pr_err("%s: %s() is not implemented.\n", GICC_FUNC_NAME, __FUNCTION__);
		return -ENOSYS;
	}

	gicc = gicc_func(0);
	if (!gicc) {
		pr_err("%s: %s(0) return NULL.\n", GICC_FUNC_NAME, __FUNCTION__);
		return -ENXIO;
	}
	gsi = gicc->performance_interrupt;

	if (acpi_gsi_to_irq(gsi, &irq)) {
		pr_err("%s: acpi_gsi_to_irq() failed.\n", __FUNCTION__);
		return -ENODEV;
	}

	if (irq_is_percpu(irq)) {
		pr_info("PMU irq is percpu.\n");
		return 0;
	}

	for (hwid = 0; hwid < SMP_MAX_CPUS; hwid++) {
		if (!(CORE_ISSET(hwid, os->cpu_hw_ids_map))) {
			continue;
		}

		gicc = gicc_func(hwid);
		if (!gicc) {
			continue;
		}
		gsi = gicc->performance_interrupt;

		if (acpi_gsi_to_irq(gsi, &irq)) {
			continue;
		}
		irqs[virtid++] = irq;
	}
	return virtid;
}
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(4,12,0) */

#else /* CONFIG_ACPI */

static int ihk_smp_acpi_get_gic_base(void)
{
	printk("ERROR: System is really ACPI environment?\n");
	return -EINVAL;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,12,0)
static inline int
ihk_armpmu_get_irq_affi_acpi(int irqs[], const struct arm_pmu *armpmu,
			     const struct smp_os_data *os)
{
	return 0;
}
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(4,12,0) */

#endif /* CONFIG_ACPI */

static int
ihk_armpmu_get_irq_affi_plat(int irqs[], const struct arm_pmu *armpmu,
			     const struct smp_os_data *os)
{
	struct platform_device* pmu_device;
	int hwid, virtid, irq;

	pmu_device = armpmu->plat_device;
	if (!pmu_device) {
		pr_err("no PMU device registered\n");
		return -ENODEV;
	}

	irq = platform_get_irq(pmu_device, 0);
	if (irq <= 0) {
		pr_err("failed to get valid irq for PMU device\n");
		return -ENODEV;
	}

	if (irq_is_percpu(irq)) {
		// TODO[PMU]: ここにくるときはPPIと予想。割込みコアは固定されているはず。
		pr_info("PMU irq is percpu.\n");
		return 0;
	}

	if (!pmu_device->num_resources) {
		pr_err("no irqs for PMUs defined\n");
		return -ENODEV;
	}

	// McKにおける論理CPU番号がインデックスになるように、
	// 物理CPU番号の若い順からirqs変数に格納しておく
	virtid = 0;
	for (hwid = 0; hwid < SMP_MAX_CPUS; hwid++) {
		int irq;

		if (!(CORE_ISSET(hwid, os->cpu_hw_ids_map))) {
			continue;
		}

		if (pmu_device->num_resources <= hwid) {
			pr_err("failed to get core number.\n");
			return -ENOENT;
		}

		irq = platform_get_irq(pmu_device, hwid);
		if (irq <= 0) {
			pr_warn("failed to get irq number.\n");
		}
		irqs[virtid++] = irq;
	}
	return virtid;
}

static int
ihk_armpmu_get_irq_affi(int irqs[], const struct arm_pmu *armpmu,
			const struct smp_os_data *os)
{
	if (!armpmu) {
		return -EINVAL;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,12,0)
	/* When CONFIG_ACPI is not defined, the acpi_disabled is always 1 */
	if (acpi_disabled) {
		return ihk_armpmu_get_irq_affi_plat(irqs, armpmu, os);
	}
	else {
		return ihk_armpmu_get_irq_affi_acpi(irqs, armpmu, os);
	}
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(4,12,0) */
	return ihk_armpmu_get_irq_affi_plat(irqs, armpmu, os);
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(4,12,0) */
}

static int ihk_smp_dt_get_gic_base(void)
{
	int result;
	struct resource res;

	struct irq_domain *domain;
	struct device_node *node;

	if(ihk_gic_data_v3->domain != NULL) {
		ihk_gic_version = 3; /* GICv3 or later */
		domain = ihk_gic_data_v3->domain;
	} else {
		pr_err("Unsupported GIC versions 2 or less.\n");
		return -EINVAL;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0)
	node = to_of_node(domain->fwnode);
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0) */
	node = domain->of_node;
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0) */

	/* get dist base. */
	result = of_address_to_resource(node, 0, &res);
	if (result == 0) {
		ihk_smp_gic_dist_base_pa = res.start;
		ihk_smp_gic_dist_size = ALIGN(res.end - res.start + 1, PAGE_SIZE);
	} else {
		printk("ERROR: GIC failed to get distributor base PA\n");
		return result;
	}

	/*
	 * Collect re-distributor base PA information
	 * that the host-linux was constructed.
	 */
	ihk_smp_gic_collect_rdist();

	return 0;
}

static int ihk_smp_collect_gic_info(void)
{
	int result = 0;

	if (!acpi_disabled){
		printk("INFO: This is the ACPI environment.\n");
		result = ihk_smp_acpi_get_gic_base();
	}
	else {
		printk("INFO: This is the Device Tree environment.\n");
		result = ihk_smp_dt_get_gic_base();
	}

	ihk_gic_max_vector = ihk_gic_data_v3->irq_nr;

	return result;
}

static inline int ihk_smp_get_cpu_affinity(int hwid, u64* affi)
{
	if (hwid < 0 || ihk___cpu_logical_map_size <= hwid) {
		printk("IHK-SMP: invalid hwid = %d\n", hwid);
		return -EINVAL;
	}
	if (affi == NULL) {
		return -EINVAL;
	}
	*affi = ihk___cpu_logical_map[hwid];
	return 0;
}

int smp_wakeup_secondary_cpu(int hw_id, unsigned long start_eip)
{
	int ret;
	u64 affi;

	ret = ihk_smp_get_cpu_affinity(hw_id, &affi);
	if (ret) {
		return ret;
	}
	D("ihk_psci_ops->cpu_on[0x%lx] (0x%llx, 0x%lx)\n",
	  (unsigned long)ihk_psci_ops->cpu_on, affi, start_eip);
	return ihk_psci_ops->cpu_on(affi, start_eip);
}

unsigned long calc_ns_per_tsc(void)
{
	unsigned int freq;

	asm volatile(
"	mrs	%0, cntfrq_el0\n"
	: "=r" (freq)
	:
	: "memory");

	return 1000000000000L / freq;
}

#ifdef CONFIG_ARM64_SVE
unsigned long get_sve_default_vl(void)
{
	struct file* filp = NULL;
	int ret, vl = 0;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,15,0)
	const char *path = "/proc/sys/abi/sve_default_vector_length";
#else
	const char *path = "/proc/cpu/sve_default_vector_length";
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(4,15,0) */
	char buf[16] = "";
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,14,0)
	loff_t pos = 0;
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(4,14,0) */

	filp = filp_open(path, O_RDONLY, 0);
	if (IS_ERR(filp)) {
		printk("%s: Leave the decision of SVE-default-VL to McKernel.\n", __FUNCTION__);
		goto open_err;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,14,0)
	ret = kernel_read(filp, buf, sizeof(buf), &pos);
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(4,14,0) */
	ret = kernel_read(filp, 0, buf, sizeof(buf));
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(4,14,0) */
	if (ret < 0) {
		printk("%s: ERROR reading %s\n", __FUNCTION__, path);
		goto read_err;
	}
	vl = strtoul(buf, NULL, 0);

read_err:
	filp_close(filp, NULL);
open_err:
	return vl;

}
#else /* CONFIG_ARM64_SVE */
unsigned long get_sve_default_vl(void)
{
	return 0;
}
#endif /* CONFIG_ARM64_SVE */

static const unsigned long *__pwr_g_retention_state_flag;
DEFINE_RAW_SPINLOCK(__retention_state_lock);

void ihk_pwr_set_retention_state_flag_address(const unsigned long *addr)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&__retention_state_lock, flags);
	__pwr_g_retention_state_flag = addr;
	raw_spin_unlock_irqrestore(&__retention_state_lock, flags);
}
EXPORT_SYMBOL(ihk_pwr_set_retention_state_flag_address);

void ihk_pwr_clear_retention_state_flag_address(void)
{
	ihk_pwr_set_retention_state_flag_address(NULL);
}
EXPORT_SYMBOL(ihk_pwr_clear_retention_state_flag_address);

void smp_ihk_setup_trampoline(void *priv)
{
	struct smp_os_data *os = priv;
	struct ihk_smp_trampoline_header *header;
	unsigned long flags;
	int nr_irqs;
	int i = 0;

	for (i = 0; i < nr_cpu_ids; i++) {
		os->param->ihk_ikc_cpu_hwids[i] = ihk_smp_get_hw_id(i);

#ifdef IHK_IKC_USE_LINUX_WORK_IRQ
		/* IRQ work per-CPU raised_list head physical addresses */
		os->param->ihk_ikc_cpu_raised_list[i] =
			(void *)virt_to_phys(per_cpu_ptr(ihk__raised_list, i));
#endif // IHK_IKC_USE_LINUX_WORK_IRQ
	}

#ifdef IHK_IKC_USE_LINUX_WORK_IRQ
	os->param->ikc_irq_work_func = (void *)smp_ihk_ikc_irq_work_func;
	os->param->ihk_ikc_irq = ihk_smp_irq;
#else
	for (i = 0; i < SMP_MAX_IRQS; i++) {
		os->param->ihk_ikc_irqs[i] = ihk_smp_irq[i].hwirq;
	}
#endif // IHK_IKC_USE_LINUX_WORK_IRQ

	/* Prepare trampoline code */
	memcpy(trampoline_va, ihk_smp_trampoline_data,
	       IHK_SMP_TRAMPOLINE_SIZE);
	D("trampoline=0x%llx, trampoline_va=0x%lx\n", __pa(trampoline_va), (unsigned long)trampoline_va);

	header = trampoline_va;
	header->page_table = ident_page_table;
	header->next_ip = os->boot_rip;
	header->notify_address = __pa(os->param);
	header->st_phys_base = virt_to_phys((void*)PAGE_OFFSET);
	header->st_phys_size = (unsigned long)high_memory - PAGE_OFFSET;
	header->dist_base_pa = ihk_smp_gic_dist_base_pa;
	header->dist_map_size = ihk_smp_gic_dist_size;
	header->cpu_base_pa = ihk_smp_gic_cpu_base_pa;
	header->cpu_map_size = ihk_smp_gic_cpu_size;
	header->percpu_offset = ihk_gic_percpu_offset;
	header->gic_version = ihk_gic_version;
	header->loops_per_jiffy = loops_per_jiffy;
	header->hz = HZ;
	header->psci_method = ihk_smp_psci_method;
	header->use_virt_timer = is_arch_timer_use_virt();
	header->evtstrm_timer_rate = (unsigned long)*ihk_arch_timer_rate;
	header->default_vl = get_sve_default_vl();
	header->cpu_logical_map_size = sizeof(header->cpu_logical_map) / sizeof(unsigned long);
	memcpy(header->cpu_logical_map, ihk___cpu_logical_map,
		header->cpu_logical_map_size * sizeof(unsigned long));
	memcpy(header->rdist_base_pa, ihk_smp_gic_rdist_pa,
		header->cpu_logical_map_size * sizeof(unsigned long));

	raw_spin_lock_irqsave(&__retention_state_lock, flags);
	if (__pwr_g_retention_state_flag) {
		header->retention_state_flag_pa = __pa(__pwr_g_retention_state_flag);
	}
	raw_spin_unlock_irqrestore(&__retention_state_lock, flags);

	nr_irqs = ihk_armpmu_get_irq_affi(header->pmu_irq_affi, *ihk_cpu_pmu, os);
	if (nr_irqs < 0) {
		header->nr_pmu_irq_affi = 0;
		return;
	}
	header->nr_pmu_irq_affi = nr_irqs;
	// TODO[PMU]: McKernel側でコアが起きた後にaffinity設定しないと駄目なら、ここでの設定は止める。
	// TODO[PMU]: A log that fails in __irq_set_affinity() in combination with CPUFW-0.8.0 or later is output.
	//ihk_armpmu_set_irq_affi(header->pmu_irq_affi, os);

	ihk___flush_dcache_area(header, IHK_SMP_TRAMPOLINE_SIZE);
}

unsigned long smp_ihk_adjust_entry(unsigned long entry,
                                          unsigned long phys)
{
	entry = entry - (IHK_SMP_MAP_KERNEL_START - phys);
	D("IHK-SMP: phys = 0x%lx, entry_va=0x%lx, entry_pa=0x%lx\n",
	  phys, (unsigned long)phys_to_virt(entry), entry);

	return entry;
}

int smp_ihk_arch_vmap_area_taken(void)
{
	int vmap_area_taken = 0;
	struct vmap_area *tmp_va;
	struct rb_node *p = ihk_vmap_area_root->rb_node;

	while (p) {
		tmp_va = rb_entry(p, struct vmap_area, rb_node);

		if (tmp_va->va_end >= IHK_SMP_MAP_KERNEL_START) {
			if (tmp_va->va_start < MODULES_END) {
				vmap_area_taken = 1;
				break;
			}
			p = p->rb_left;
		}
		else {
			p = p->rb_right;
		}
	}
	return vmap_area_taken;
}

int smp_ihk_os_setup_startup(void *priv, unsigned long phys,
                            unsigned long entry)
{
	struct smp_os_data *os = priv;
	extern char startup_data[];
	extern char startup_data_end[];
	unsigned long startup_p;
	unsigned long *startup;

	startup_p = os->bootstrap_mem_end - (2 << IHK_SMP_LARGE_PAGE_SHIFT);
	D("startup_p=0x%lx\n", startup_p);
	startup = ihk_smp_map_virtual(startup_p, PAGE_SIZE);
	memcpy(startup, startup_data, startup_data_end - startup_data);
//	startup[2] = pml4_p;
	startup[3] = 0xffffffffc0000000;
	startup[4] = phys;
	startup[5] = trampoline_phys;
	startup[6] = entry;
	ihk_smp_unmap_virtual(startup);
	os->boot_rip = startup_p;
	return 0;
}

enum ihk_os_status smp_ihk_os_query_status(ihk_os_t ihk_os, void *priv)
{
	int n;
	int i;
	int freezing;
	int ret;
	struct smp_os_data *os = priv;
	struct ihk_host_linux_os_data *data = ihk_os;
	int status;

	status = os->status;
	pr_info("%s: builtin os status: %d",
		__func__, status);

	switch (status) {
	case BUILTIN_OS_STATUS_BOOTING:
		if (os->param->status == 1) {
			ret = IHK_OS_STATUS_BOOTED;
		} else if(os->param->status == 2) {
			ret = IHK_OS_STATUS_READY;
		} else if(os->param->status == 3) {
			ret = IHK_OS_STATUS_RUNNING;
		} else {
			ret = IHK_OS_STATUS_BOOTING;
		}
		break;
	case BUILTIN_OS_STATUS_HUNGUP:
		ret = IHK_OS_STATUS_HUNGUP;
		break;
	case BUILTIN_OS_STATUS_SHUTDOWN:
		ret = IHK_OS_STATUS_SHUTDOWN;
		break;
	default:
		ret = IHK_OS_STATUS_NOT_BOOTED;
		break;
	}

	pr_info("%s: status before checking monitor info: %d",
		__func__, ret);

	if (ret != IHK_OS_STATUS_READY && ret != IHK_OS_STATUS_RUNNING)
		goto out;

	setup_monitor(data);
	if (data->monitor == NULL) {
		ret = -ENOSYS;
		goto out;
	}

	n = data->monitor->num_processors;
	for (i = 0; i < n; i++) {
		if (data->monitor->cpu[i].status == IHK_OS_MONITOR_PANIC) {
			pr_info("%s: cpu[%d].status==%d\n",
				__func__, i, data->monitor->cpu[i].status);
			ret = IHK_OS_STATUS_FAILED;
			goto out;
		}

	}

	freezing = data->monitor->cpu[0].status;
	if (freezing == IHK_OS_MONITOR_KERNEL_FREEZING) {
		ret = IHK_OS_STATUS_FREEZING;
		goto out;
	}

	for (i = 1; i < n; i++) {
		switch (data->monitor->cpu[i].status) {
		    case IHK_OS_MONITOR_KERNEL_FREEZING:
			ret = IHK_OS_STATUS_FREEZING;
			goto out;
		    case IHK_OS_MONITOR_KERNEL_FROZEN:
			if (freezing != IHK_OS_MONITOR_KERNEL_FROZEN) {
				ret = IHK_OS_STATUS_FREEZING;
				goto out;
			}
			break;
		    default:
			if (freezing == IHK_OS_MONITOR_KERNEL_FROZEN) {
				ret = IHK_OS_STATUS_FREEZING;
				goto out;
			}
			break;
		}
	}
	if (freezing == IHK_OS_MONITOR_KERNEL_FROZEN) {
		ret = IHK_OS_STATUS_FROZEN;
		goto out;
	}

 out:
	pr_info("%s: status after checking monitor info: %d",
		__func__, ret);

	return ret;
}

int smp_ihk_os_send_nmi(ihk_os_t ihk_os, void *priv, int mode)
{
	struct smp_os_data *os = priv;
	int i, ret;

	ret = ihk_smp_set_nmi_mode(ihk_os, priv, mode);
	if (ret) {
		return ret;
	}

	/* mode == 0,    for MEMDUMP NMI */
	for (i = 0; i < os->cpu_info.n_cpus; ++i) {
		int hwid = os->cpu_info.hw_ids[i];

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,1,0)
		ihk___smp_cross_call(cpumask_of(hwid), INTRID_MULTI_NMI);
#else
		ihk___smp_cross_call(&cpumask_of_cpu(hwid), INTRID_MULTI_NMI);
#endif
		dprintk("send to NMI CPU:%d\n", hwid);
	}
	return 0;
}

int smp_ihk_os_send_multi_intr(ihk_os_t ihk_os, void *priv, int mode)
{
	struct smp_os_data *os = priv;
	int i, ret;

	ret = ihk_smp_set_multi_intr_mode(ihk_os, priv, mode);
	if (ret) {
		return ret;
	}

	/* mode == 1or2, for FREEZER INTR */
	for (i = 0; i < os->cpu_info.n_cpus; ++i) {
		int hwid = os->cpu_info.hw_ids[i];

#if KERNEL_VERSION(4, 1, 0) <= LINUX_VERSION_CODE
		ihk___smp_cross_call(cpumask_of(hwid), INTRID_MULTI_INTR);
#else
		ihk___smp_cross_call(&cpumask_of_cpu(hwid), INTRID_MULTI_INTR);
#endif
		dprintk("send to INTR CPU:%d\n", hwid);
	}
	return 0;
}

static long get_dump_num_mem_areas(struct smp_os_data *os)
{
	struct ihk_dump_page *dump_page = NULL;
	int i, j, k, mem_num;
	unsigned long bit_count;

	while (1) {
		if (IHK_DUMP_PAGE_SET_COMPLETED == os->param->dump_page_set.completion_flag) {
			break;
		}
		msleep(10); /* 10ms sleep */
	}
	dump_page = phys_to_virt(os->param->dump_page_set.phy_page);

	for (i = 0, mem_num = 0; i < os->param->dump_page_set.count; i++) {
		if (i) {
			dump_page = (struct ihk_dump_page *)((char *)dump_page + ((dump_page->map_count * sizeof(unsigned long)) + sizeof(struct ihk_dump_page)));
		}

		for (j = 0, bit_count = 0; j < dump_page->map_count; j++) {
			for (k = 0; k < 64; k++) {
				if ((dump_page->map[j] >> k) & 0x1) {
					bit_count++;
				}
				else {
					if (bit_count) {
						mem_num++;
						bit_count = 0;
					}
				}
			}
		}

		if (bit_count) {
			mem_num++;
		}
	}
	return (sizeof(dump_mem_chunks_t) + (sizeof(struct dump_mem_chunk) * mem_num));
}

int smp_ihk_os_dump(ihk_os_t ihk_os, void *priv, dumpargs_t *args)
{
	struct smp_os_data *os = priv;
	struct ihk_dump_page *dump_page = NULL;
	int i,j,k,index;
	long mem_size;
	struct ihk_os_mem_chunk *os_mem_chunk;
	unsigned long map_start, bit_count;
	dump_mem_chunks_t *mem_chunks;
	void *va;
	extern struct list_head ihk_mem_used_chunks;

	if (0) printk("mcosdump: cmd %d start %lx size %lx buf %p\n",
			args->cmd, args->start, args->size, args->buf);

	switch (args->cmd) {
	case DUMP_SET_LEVEL:
		/* Set dump level information */
		switch (args->level) {
			case DUMP_LEVEL_ALL:
			case DUMP_LEVEL_USER_UNUSED_EXCLUDE:
				os->param->dump_level = args->level;
				break;
			default:
				printk("%s:invalid dump level:%d\n", __FUNCTION__, args->level);
				return -EINVAL;
		}
		break;

	case DUMP_NMI:
		if (os->param->dump_page_set.completion_flag !=  IHK_DUMP_PAGE_SET_COMPLETED) {
			smp_ihk_os_send_nmi(ihk_os, priv, 0);
		}
		break;

	case DUMP_NMI_CONT:
		if (os->param->dump_page_set.completion_flag ==
				IHK_DUMP_PAGE_SET_COMPLETED) {
			smp_ihk_os_send_nmi(ihk_os, priv, 4);
		}
		break;

	case DUMP_QUERY_NUM_MEM_AREAS:
		args->size = get_dump_num_mem_areas(os);
		break;

	case DUMP_QUERY:
		i = 0;
		mem_size = get_dump_num_mem_areas(os);
		mem_chunks = kmalloc(mem_size, GFP_KERNEL);
		if (!mem_chunks) {
			printk("%s: memory allocation failed.\n", __FUNCTION__);
			return -ENOMEM;
		}
		memset(mem_chunks, 0, mem_size);

		/* Collect memory information */
		list_for_each_entry(os_mem_chunk, &ihk_mem_used_chunks, list) {
			if (os_mem_chunk->os != ihk_os)
				continue;

			mem_chunks->chunks[i].addr = os_mem_chunk->addr;
			mem_chunks->chunks[i].size = os_mem_chunk->size;
			++i;
		}

		mem_chunks->nr_chunks = i;
		/* See load_file() for the calculation below */
		mem_chunks->kernel_base =
			(os->bootstrap_mem_start + IHK_SMP_LARGE_PAGE * 2 - 1) & IHK_SMP_LARGE_PAGE_MASK;
		mem_chunks->phys_start = *ihk___memstart_addr;

		if (copy_to_user(args->buf, mem_chunks, mem_size)) {
			printk("%s: copy_to_user failed.\n", __FUNCTION__);
			kfree(mem_chunks);
			return -EFAULT;
		}
		kfree(mem_chunks);
		break;

	case DUMP_QUERY_MEM_AREAS:
		mem_size = get_dump_num_mem_areas(os);
		mem_chunks = kmalloc(mem_size, GFP_KERNEL);
		if (!mem_chunks) {
			printk("%s: memory allocation failed.\n", __FUNCTION__);
			return -ENOMEM;
		}
		memset(mem_chunks, 0, mem_size);

		dump_page = phys_to_virt(os->param->dump_page_set.phy_page);

		for (i = 0, index = 0; i < os->param->dump_page_set.count; i++) {

			if (i) {
				dump_page = (struct ihk_dump_page *)((char *)dump_page + ((dump_page->map_count * sizeof(unsigned long)) + sizeof(struct ihk_dump_page)));
			}

			for (j = 0, bit_count = 0; j < dump_page->map_count; j++) {
				for (k = 0; k < 64; k++) {
					if ((dump_page->map[j] >> k) & 0x1) {
						if (!bit_count) {
							map_start = (unsigned long)(dump_page->start + ((unsigned long)j << (PAGE_SHIFT+6)));
							map_start = map_start + ((unsigned long)k << PAGE_SHIFT);
						}
						bit_count++;
					} else {
						if (bit_count) {
							mem_chunks->chunks[index].addr = map_start;
							mem_chunks->chunks[index].size = (bit_count << PAGE_SHIFT);
							index++;
							bit_count = 0;
						}
					}
				}
			}

			if (bit_count) {
				mem_chunks->chunks[index].addr = map_start;
				mem_chunks->chunks[index].size = (bit_count << PAGE_SHIFT);
				index++;
			}
		}

		mem_chunks->nr_chunks = index;

		/* See load_file() for the calculation below */
		mem_chunks->kernel_base =
			(os->bootstrap_mem_start + IHK_SMP_LARGE_PAGE * 2 - 1) & IHK_SMP_LARGE_PAGE_MASK;
		mem_chunks->phys_start = *ihk___memstart_addr;

		if (copy_to_user(args->buf, mem_chunks, mem_size)) {
			printk("%s: copy_to_user failed.\n", __FUNCTION__);
			kfree(mem_chunks);
			return -EFAULT;
		}
		kfree(mem_chunks);
		break;

	case DUMP_READ:
		va = phys_to_virt(args->start);
		if (copy_to_user(args->buf, va, args->size)) {
			return -EFAULT;
		}

		break;

	case DUMP_QUERY_ALL:
		args->start = os->mem_start;
		args->size = os->mem_end - os->mem_start;
		break;

	case DUMP_QUERY_PHYS_START:
		if (copy_to_user(args->buf, ihk___memstart_addr,
					sizeof(uint64_t))) {
			return -EFAULT;
		}

		break;

	case DUMP_READ_ALL:
		va = phys_to_virt(args->start);
		if (copy_to_user(args->buf, va, args->size)) {
			return -EFAULT;
		}
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

int smp_ihk_os_issue_interrupt(ihk_os_t ihk_os, void *priv,
                               int cpu, int v)
{
	struct smp_os_data *os = priv;

	/* better calcuation or make map */
	if (cpu < 0 || cpu >= os->cpu_info.n_cpus) {
		return -EINVAL;
	}
//	printk("smp_ihk_os_issue_interrupt(): %d\n", os->cpu_info.hw_ids[cpu]);

	smp_mb();
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,1,0)
	ihk___smp_cross_call(cpumask_of(os->cpu_info.hw_ids[cpu]), v);
#else
	ihk___smp_cross_call(&cpumask_of_cpu(os->cpu_info.hw_ids[cpu]), v);
#endif

	return -EINVAL;
}

unsigned long smp_ihk_os_map_memory(ihk_os_t ihk_os, void *priv,
                                    unsigned long remote_phys,
                                    unsigned long size)
{
	/* We use the same physical memory. So no need to do something */
	return remote_phys;
}

int smp_ihk_os_unmap_memory(ihk_os_t ihk_os, void *priv,
                            unsigned long local_phys,
                            unsigned long size)
{
	return 0;
}

/** \brief Map a remote physical memory to the local physical memory.
 *
 * In BUILTIN, all the kernels including the host kernel are running in the
 * same physical memory map, thus there is nothing to do. */
unsigned long smp_ihk_map_memory(ihk_device_t ihk_dev, void *priv,
                                 unsigned long remote_phys,
                                 unsigned long size)
{
	/* We use the same physical memory. So no need to do something */
	return remote_phys;
}

int smp_ihk_unmap_memory(ihk_device_t ihk_dev, void *priv,
                         unsigned long local_phys,
                         unsigned long size)
{
	return 0;
}

#ifndef IHK_IKC_USE_LINUX_WORK_IRQ
static irqreturn_t smp_ihk_irq_handler(int irq, void *dev_id)
{
	smp_ihk_irq_call_handlers(irq, NULL);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,3,0))
	/* temporary fix */
	irq_set_irqchip_state(irq, IRQCHIP_STATE_PENDING, false);
	/* temporary fix */
#endif

	return IRQ_HANDLED;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0)
static int ihk_smp_reserve_irq(struct ihk_smp_irq_table *smp_irq,
				unsigned int start_irq, int nr)
{
	unsigned int virq, hwirq;
	struct irq_domain *domain;
	struct irq_fwspec fw_args;
	struct irq_desc *desc;

	if (start_irq < 32){
		hwirq = 32; // base of SPI.
	} else if (start_irq > ihk_gic_max_vector) {
		hwirq = ihk_gic_max_vector - 1;
	} else {
		hwirq = start_irq;
	}

	domain = ihk_gic_data_v3->domain;

	for( ; hwirq < ihk_gic_max_vector; hwirq += 1 ) {
		// check hwirq is in used?
		virq = irq_find_mapping(domain, hwirq);
		if (virq == 0) {
			break;
		}
	}

	if(hwirq == ihk_gic_max_vector) {
		printk("IRQ vector : There is no blank irq\n");
		return -ENOENT;
	}

	if (is_of_node(domain->fwnode)) {
		fw_args.fwnode = domain->fwnode;
		fw_args.param_count = 3;
		fw_args.param[0] = GIC_SPI;
		fw_args.param[1] = hwirq - 32;
		fw_args.param[2] = IRQ_TYPE_LEVEL_HIGH;
	} else if (is_fwnode_irqchip(domain->fwnode)) {
		fw_args.fwnode = domain->fwnode;
		fw_args.param_count = 2;
		fw_args.param[0] = hwirq;
		fw_args.param[1] = IRQ_TYPE_LEVEL_HIGH;
	}
	virq = irq_create_fwspec_mapping(&fw_args);

	desc = irq_to_desc(virq);
	if (!desc) {
		printk("IRQ vector %d: still no descriptor??\n", virq);
		return -EINVAL;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0)
	if (desc->status_use_accessors & IRQ_NOREQUEST) {
		printk("IRQ vector %d: not allowed to request, fake it\n",
		       virq);

		desc->status_use_accessors &= ~IRQ_NOREQUEST;
	}
#else
	if (desc->status & IRQ_NOREQUEST) {
		printk("IRQ vector %d: not allowed to request, fake it\n",
		       virq);

		desc->status &= ~IRQ_NOREQUEST;
	}
#endif
	snprintf(smp_irq->irq_name, sizeof(smp_irq->irq_name), "IHK-SMP%d", nr);
	if (request_irq(virq, smp_ihk_irq_handler,
			IRQF_NOBALANCING, smp_irq->irq_name, NULL) != 0) {
		pr_info("IHK-SMP: IRQ vector %d: request_irq failed\n", virq);
		return -EFAULT;
	}

	smp_irq->irq = virq;
	smp_irq->hwirq = (u32)(irq_desc_get_irq_data(desc)->hwirq);
	pr_info("IHK-SMP: IKC irq vector: %d, hwirq#: %d\n",
			smp_irq->irq, smp_irq->hwirq);

	return virq;
}
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0) */
static int ihk_smp_reserve_irq(struct ihk_smp_irq_table *smp_irq,
				unsigned int start_irq, int nr)
{
	unsigned int vector;
	int error;

	/* Find a suitable IRQ vector */
	for (vector = start_irq ? start_irq : ihk_gic_max_vector/3;
		vector < ihk_gic_max_vector; vector += 1) {
		struct irq_desc *desc;

#ifdef CONFIG_SPARSE_IRQ
		desc = irq_to_desc(vector);
		if (!desc) {
			int result;
			struct irq_domain *domain;

			struct of_phandle_args of_args;
			domain = ihk_gic_data_v3->domain;

			of_args.np = domain->of_node;
			of_args.args[0] = GIC_SPI;
			of_args.args[1] = vector;
			of_args.args[2] = IRQ_TYPE_LEVEL_HIGH;
			of_args.args_count = 3;

			result = ihk___irq_domain_alloc_irqs(
					domain,			/* struct irq_domain *domain */
					vector,			/* int irq_base */
					1,			/*  unsigned int nr_irqs */
					-1,			/* int node */
					(void *)&of_args,	/* void *arg */
					false);			/* bool realloc */

			if (result <= 0){
				printk("IRQ vector %d: irq_domain_alloc_irqs failed.(%d)\n", vector, result);
				continue;
			}
			vector = result;
		} else {
			printk("IRQ vector %d: has descriptor\n", vector);
			continue;
		}

		desc = irq_to_desc(vector);
		if (!desc) {
			printk(KERN_INFO "IHK-SMP: IRQ vector %d: failed allocating descriptor\n", vector);
			continue;
		}

		if (desc->status_use_accessors & IRQ_NOREQUEST) {
			printk(KERN_INFO "IHK-SMP: IRQ vector %d: not allowed to request, fake it\n", vector);

			desc->status_use_accessors &= ~IRQ_NOREQUEST;
		}
#endif /* CONFIG_SPARSE_IRQ */

		snprintf(smp_irq->irq_name, sizeof(smp_irq->irq_name), "IHK-SMP%d", nr);
		if (request_irq(vector, smp_ihk_irq_handler,
			IRQF_NOBALANCING, smp_irq->irq_name, NULL) != 0) {
			printk(KERN_INFO "IHK-SMP: IRQ vector %d: request_irq failed\n", vector);

			irq_free_descs(vector, 1);
			continue;
		}

		/* get HwIRQ# through desc->irq_data */
		smp_irq->hwirq = (u32)(irq_desc_get_irq_data(desc)->hwirq);
		break;
	}

	if (vector >= ihk_gic_max_vector) {
		printk("IHK-SMP: error: allocating IKC irq vector\n");
		error = EFAULT;
		goto error_free_trampoline;
	}

	printk("IHK-SMP: IKC irq vector: %d, hwirq#: %d\n", vector,
	       smp_irq->hwirq);
	smp_irq->irq = vector;
	return vector;

error_free_trampoline:
	if (trampoline_page) {
		free_pages((unsigned long)pfn_to_kaddr(page_to_pfn(trampoline_page)), 1);
	}
	return error;
}
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0) */

static void ihk_smp_release_irq(struct ihk_smp_irq_table *smp_irq)
{
	if (smp_irq->irq == -1) {
		return;
	}

	free_irq(smp_irq->irq, NULL);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0)
	irq_dispose_mapping(smp_irq->irq);
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0) */
#ifdef CONFIG_SPARSE_IRQ
	ihk_irq_domain_free_irqs(smp_irq->irq, 1);
#endif /* CONFIG_SPARSE_IRQ */
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0) */
}
#endif // !IHK_IKC_USE_LINUX_WORK_IRQ

int ihk_pwr_mck_request(void** handle)
{
	int ret = 0;
	ihk_os_t ihk_os;
	int cpu;

	ret = ihk_get_request_os_cpu(&ihk_os, &cpu);
	if (ret) {
		*handle = NULL;
		if (ret == -EINVAL) {
			// オフロードからの呼び出しではない場合
			ret = 0;
		}
		goto out;
	}
	*handle = (void *)ihk_os;
out:
	return ret;
}
EXPORT_SYMBOL(ihk_pwr_mck_request);

#if 0 /* no support */
int ihk_pwr_linux_to_mck(void *handle, int linux_cpu)
{
	int ret = 0;
	ihk_os_t ihk_os;
	struct ihk_cpu_info* info;
	int i;

	if (handle == NULL) {
		ret = -EINVAL;
		goto out;
	}

	ihk_os = (ihk_os_t)handle;

	info = ihk_os_get_cpu_info(ihk_os);
	if (info == NULL) {
		ret = -EFAULT;
		goto out;
	}

	ret = -EINVAL;
	for (i = 0; i < info->n_cpus; i++) {
		if (info->mapping[i] == linux_cpu) {
			ret = i;
			break;
		}
	}
out:
	return ret;
}
EXPORT_SYMBOL(ihk_pwr_linux_to_mck);
#endif /* no support */

int ihk_pwr_mck_to_linux(void *handle, int mck_cpu)
{
	int ret = 0;
	ihk_os_t ihk_os;
	struct ihk_cpu_info* info;

	if (handle == NULL) {
		ret = -EINVAL;
		goto out;
	}
	ihk_os = (ihk_os_t)handle;

	info = ihk_os_get_cpu_info(ihk_os);
	if (info == NULL) {
		ret = -EFAULT;
		goto out;
	}

	if (0 <= mck_cpu && mck_cpu < info->n_cpus) {
		ret = info->mapping[mck_cpu];
	} else {
		ret = -EINVAL;
	}
out:
	return ret;
}
EXPORT_SYMBOL(ihk_pwr_mck_to_linux);

static DEFINE_RWLOCK(ihk_pwr_ipi_register_lock);

static int ihk_pwr_ipi_read_register_locked(void *handle, int mck_cpu, u32 sys_reg, u64* value)
{
	int ret = 0;
	ihk_os_t ihk_os;
	struct ihk_os_cpu_register desc;

	if (handle == NULL) {
		ret = -EINVAL;
		goto out;
	}
	ihk_os = (ihk_os_t)handle;
	desc.addr = 0;
	desc.addr_ext = sys_reg;

	ret = ihk_os_read_cpu_register(ihk_os, mck_cpu, &desc);
	if (ret) {
		goto out;
	}
	// TODO[確認] struct ihk_os_cpu_register::syncが未定義。IPIの同期はihk_os_read_cpu_register内部でとる？
	*value = desc.val;
out:
	return ret;
}

static int ihk_pwr_ipi_read_register(void *handle, int mck_cpu, u32 sys_reg, u64* value)
{
	int ret;
	unsigned long flags;

	read_lock_irqsave(&ihk_pwr_ipi_register_lock, flags);
	ret = ihk_pwr_ipi_read_register_locked(handle, mck_cpu, sys_reg, value);
	read_unlock_irqrestore(&ihk_pwr_ipi_register_lock, flags);
	return ret;
}
EXPORT_SYMBOL(ihk_pwr_ipi_read_register);

static int ihk_pwr_ipi_write_register_locked(void *handle, int mck_cpu, u32 sys_reg, u64 set_bit, u64 clear_bit)
{
	int ret = 0;
	ihk_os_t ihk_os;
	struct ihk_os_cpu_register desc;
	u64 value;

	ret = ihk_pwr_ipi_read_register_locked(handle, mck_cpu, sys_reg, &value);
	if (ret) {
		goto out;
	}
	ihk_os = (ihk_os_t)handle;

	desc.addr = 0;
	desc.addr_ext = sys_reg;
	desc.val = (value & ~clear_bit) | set_bit;
	ret = ihk_os_write_cpu_register(ihk_os, mck_cpu, &desc);
	if (ret) {
		goto out;
	}
	// TODO[確認] struct ihk_os_cpu_register::syncが未定義。IPIの同期はihk_os_write_cpu_register内部でとる？
out:
	return ret;
}

static int ihk_pwr_ipi_write_register(void *handle, int mck_cpu, u32 sys_reg, u64 set_bit, u64 clear_bit)
{
	int ret;
	unsigned long flags;

	write_lock_irqsave(&ihk_pwr_ipi_register_lock, flags);
	ret = ihk_pwr_ipi_write_register_locked(handle, mck_cpu, sys_reg, set_bit, clear_bit);
	write_unlock_irqrestore(&ihk_pwr_ipi_register_lock, flags);
	return ret;
}
EXPORT_SYMBOL(ihk_pwr_ipi_write_register);

static int collect_topology(void);
int smp_ihk_arch_init(void)
{
	int error;
	int i;
#ifndef IHK_IKC_USE_LINUX_WORK_IRQ
	unsigned int start_irq = ihk_start_irq;
#endif // !IHK_IKC_USE_LINUX_WORK_IRQ

	/* psci_method check */
	if (ihk_invoke_psci_fn) {
		if (*ihk_invoke_psci_fn == ihk___invoke_psci_fn_smc) {
			ihk_smp_psci_method = PSCI_METHOD_SMC;
		} else if (*ihk_invoke_psci_fn == ihk___invoke_psci_fn_hvc) {
			ihk_smp_psci_method = PSCI_METHOD_HVC;
		}
	}

	if (ihk_smp_psci_method == PSCI_METHOD_INVALID) {
		printk("IHK-SMP: error: psci_method neither SMC nor HVC\n");
		return EFAULT;
	}

	if (ihk_trampoline) {
		printk("IHK-SMP: preallocated trampoline phys: 0x%lx\n",
		       ihk_trampoline);

		trampoline_phys = ihk_trampoline;
		trampoline_va = ioremap_cache(trampoline_phys, PAGE_SIZE);

	}
	else {
#define TRAMP_ATTEMPTS  20
		int attempts = 0;
		int order;
		struct page *bad_pages[TRAMP_ATTEMPTS];

		memset(bad_pages, 0, TRAMP_ATTEMPTS * sizeof(struct page *));
		order = get_order(IHK_SMP_TRAMPOLINE_SIZE);
retry_trampoline:
		trampoline_page = alloc_pages(GFP_DMA | GFP_KERNEL, order);
		
		if (!trampoline_page) {
			bad_pages[attempts] = trampoline_page;
			
			if (++attempts < TRAMP_ATTEMPTS) {
				printk("IHK-SMP: warning: retrying trampoline_code allocation\n");
				goto retry_trampoline;
			}
			
			printk("IHK-SMP: error: allocating trampoline_code\n");
			return -EFAULT;
		}
		
		/* Free failed attempts.. */
		for (attempts = 0; attempts < TRAMP_ATTEMPTS; ++attempts) {
			if (!bad_pages[attempts]) {
				continue;
			}
			
			free_pages((unsigned long)pfn_to_kaddr(page_to_pfn(bad_pages[attempts])),
			           order);
		}
		
		trampoline_phys = page_to_phys(trampoline_page);
		trampoline_va =
			pfn_to_kaddr(page_to_pfn(trampoline_page));
		
		printk(KERN_INFO "IHK-SMP: trampoline_page phys: 0x%lx\n", trampoline_phys);
	}
	
	{
		/* Get GIC register base physical address */
		int result = ihk_smp_collect_gic_info();
		if(result != 0 || ihk_gic_max_vector <= 0) {
			printk("IHK-SMP: error: get GIC base pa\n");
			return -EINVAL;
		}
	}

#ifdef IHK_IKC_USE_LINUX_WORK_IRQ
	/* Use Linux IRQ work SGI interrupt for IKC IPI */
	for (i = 0; i < NR_IPI; ++i) {
		if (!strcmp(ihk__ipi_types[i], "IRQ work interrupts")) {
			ihk_smp_irq = i;
			break;
		}
	}

	if (ihk_smp_irq == -1) {
		printk("IHK-SMP: error: couldn't find Linux work IRQ\n");
		error = -EFAULT;
		goto error_free_irq;
	}
	printk("IHK-SMP: using Linux work IRQ (%d) for IKC IPI\n",
			ihk_smp_irq);
#else
	/* check module_param ihk_nr_irq */
	if (SMP_MAX_IRQS < ihk_nr_irq) {
		printk("%s: Since ihk_nr_irq is larger than %d,\n",
			__FUNCTION__, SMP_MAX_IRQS);
		return -EINVAL;
	}

	/* initialized ihk_smp_irq array */
	for (i = 0; i < SMP_MAX_IRQS; i++) {
		ihk_smp_irq[i].irq = -1;
		ihk_smp_irq[i].hwirq = -1;
	}

	for (i = 0; i < ihk_nr_irq; i++) {
		/* do request_irq for IKC */
		error = ihk_smp_reserve_irq(&ihk_smp_irq[i], start_irq, i);
		if (error <= 0) {
			printk("IHK-SMP%d: error: request IRQ faild.(ret=%d)\n", i, error);
			goto error_free_irq;
		}
		start_irq = ihk_smp_irq[i].hwirq + 1;
	}
#endif // IHK_IKC_USE_LINUX_WORK_IRQ

	error = collect_topology();
	if (error) {
		goto error_free_irq;
	}

	return error;

error_free_irq:
#if LINUX_VERSION_CODE <= KERNEL_VERSION(4, 3, 0)
	if (this_module_put) {
		try_module_get(THIS_MODULE);
	}
#endif

#ifndef IHK_IKC_USE_LINUX_WORK_IRQ
	for (i = 0; i < ihk_nr_irq; i++) {
		ihk_smp_release_irq(&ihk_smp_irq[i]);
	}
#endif // !IHK_IKC_USE_LINUX_WORK_IRQ
	return error;
}

/*
 * @ref.impl arch/arm64/kernel/psci.c
 */
static int ihk_smp_cpu_kill(unsigned int hwid, u64 affi)
{
	int err, i;

	for (i = 0; i < 10; i++) {
		err = ihk_psci_ops->affinity_info(affi, 0);
		if (err == PSCI_0_2_AFFINITY_LEVEL_OFF) {
			pr_info("IHK-SMP: CPU HWID %d killed.\n", hwid);
			return 0;
		}

		msleep(10);
		pr_info("IHK-SMP: Retrying again to check for CPU kill\n");
	}

	pr_warn("IHK-SMP: CPU HWID %d may not have shut down cleanly (AFFINITY_INFO reports %d)\n",
	        hwid, err);

	/* Make op_cpu_kill() fail. */
	return -ETIMEDOUT;
}

LIST_HEAD(cpu_topology_list);
LIST_HEAD(node_topology_list);

static int ensure_continue(const char *errmsg, int result)
{
	int is_error = 0;

	switch (result) {
		case 0:
			// no problem.
			break;
		case -ENOENT:
			// When target file does not exist,
			// processes may be continued.
			break;
		default:
			is_error = 1;
			printk(errmsg, result);
			break;
	}
	return is_error;
}

static int collect_cache_topology(struct ihk_cpu_topology *cpu_topo, int index)
{
	int error;
	char *prefix = NULL;
	int n;
	struct ihk_cache_topology *p = NULL;

	dprintk("collect_cache_topology(%p,%d)\n", cpu_topo, index);
	prefix = kmalloc(PATH_MAX, GFP_KERNEL);
	if (!prefix) {
		error = -ENOMEM;
		eprintk("ihk:collect_cache_topology:"
				"kmalloc failed. %d\n", error);
		goto out;
	}

	n = snprintf(prefix, PATH_MAX,
			"/sys/devices/system/cpu/cpu%d/cache/index%d",
			cpu_topo->cpu_number, index);
	if (n >= PATH_MAX) {
		error = -ENAMETOOLONG;
		eprintk("ihk:collect_cache_topology:"
				"snprintf failed. %d\n", error);
		goto out;
	}

	if (!file_readable("%s/level", prefix)) {
		/* File doesn't exist, it's not an error */
		error = 0;
		goto out;
	}

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p) {
		error = -ENOMEM;
		eprintk("ihk:collect_cache_topology:"
				"kzalloc failed. %d\n", error);
		goto out;
	}

	p->index = index;

	error = read_long(&p->level, "%s/level", prefix);
	if(ensure_continue(KERN_ERR "ihk:collect_cache_topology:"
				"read_long(level) failed. %d\n", error)) {
		goto out;
	}

	error = read_string(&p->type, "%s/type", prefix);
	if(ensure_continue(KERN_ERR "ihk:collect_cache_topology:"
				"read_string(type) failed. %d\n", error)) {
		goto out;
	}

	error = read_long(&p->size, "%s/size", prefix);
	if(ensure_continue(KERN_ERR "ihk:collect_cache_topology:"
				"read_long(size) failed. %d\n", error)) {
		goto out;
	}
	p->size *= 1024;	/* XXX */

	error = read_string(&p->size_str, "%s/size", prefix);
	if(ensure_continue(KERN_ERR "ihk:collect_cache_topology:"
				"read_string(size) failed. %d\n", error)) {
		goto out;
	}

	error = read_long(&p->coherency_line_size,
			"%s/coherency_line_size", prefix);
	if(ensure_continue(KERN_ERR "ihk:collect_cache_topology:"
				"read_long(coherency_line_size) failed. %d\n",
				error)) {
		goto out;
	}

	error = read_long(&p->number_of_sets, "%s/number_of_sets", prefix);
	if(ensure_continue(KERN_ERR "ihk:collect_cache_topology:"
				"read_long(number_of_sets) failed. %d\n",
				error)) {
		goto out;
	}

	error = read_long(&p->ways_of_associativity,
			"%s/ways_of_associativity", prefix);
	if(ensure_continue(KERN_ERR "ihk:collect_cache_topology:"
				"read_long(ways_of_associativity) failed."
				" %d\n", error)) {
		goto out;
	}

	error = read_bitmap(&p->shared_cpu_map, nr_cpumask_bits,
			"%s/shared_cpu_map", prefix);
	if(ensure_continue(KERN_ERR "ihk:collect_cache_topology:"
				"read_bitmap(shared_cpu_map) failed. %d\n",
				error)) {
		goto out;
	}

	error = 0;
	list_add(&p->chain, &cpu_topo->cache_topology_list);
	p = NULL;

out:
	if (p) {
		kfree(p->type);
		kfree(p->size_str);
		kfree(p);
	}
	kfree(prefix);
	dprintk("collect_cache_topology(%p,%d): %d\n", cpu_topo, index, error);
	return error;
} /* collect_cache_topology() */

static int collect_cpu_topology(int cpu)
{
	int error;
	char *prefix = NULL;
	int n;
	struct ihk_cpu_topology *p = NULL;
	int index;

	dprintk("collect_cpu_topology(%d)\n", cpu);
	prefix = kmalloc(PATH_MAX, GFP_KERNEL);
	if (!prefix) {
		error = -ENOMEM;
		eprintk("ihk:collect_cpu_topology:"
				"kmalloc failed. %d\n", error);
		goto out;
	}

	n = snprintf(prefix, PATH_MAX, "/sys/devices/system/cpu/cpu%d", cpu);
	if (n >= PATH_MAX) {
		error = -ENAMETOOLONG;
		eprintk("ihk:collect_cpu_topology:"
				"snprintf failed. %d\n", error);
		goto out;
	}

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p) {
		error = -ENOMEM;
		eprintk("ihk:collect_cpu_topology:"
				"kzalloc failed. %d\n", error);
		goto out;
	}

	INIT_LIST_HEAD(&p->cache_topology_list);
	p->cpu_number = cpu;
	p->hw_id = cpu;

	error = read_long(&p->core_id, "%s/topology/core_id", prefix);
	if(ensure_continue(KERN_ERR "ihk:collect_cpu_info:"
				"read_long(core_id) failed. %d\n", error)) {
		goto out;
	}

	error = read_bitmap(&p->core_siblings, nr_cpumask_bits,
			"%s/topology/core_siblings", prefix);
	if(ensure_continue(KERN_ERR "ihk:collect_cpu_info:"
				"read_bitmap(core_siblings) failed. %d\n",
				error)) {
		goto out;
	}

	error = read_long(&p->physical_package_id,
			"%s/topology/physical_package_id", prefix);
	if(ensure_continue(KERN_ERR "ihk:collect_cpu_info:"
				"read_long(physical_package_id) failed. %d\n",
				error)) {
		goto out;
	}

	error = read_bitmap(&p->thread_siblings, nr_cpumask_bits,
			"%s/topology/thread_siblings", prefix);
	if(ensure_continue(KERN_ERR "ihk:collect_cpu_info:"
				"read_bitmap(thread_siblings) failed. %d\n",
				error)) {
		goto out;
	}

	for (index = 0; index < 10; ++index) {
		error = collect_cache_topology(p, index);
		if (error) {
			dprintk("collect_cpu_info:"
					"collect_cache_topology(%d) failed."
					" %d\n", index, error);
			break;
		}
	}

	error = 0;
	list_add(&p->chain, &cpu_topology_list);
	p = NULL;

out:
	kfree(p);
	kfree(prefix);
	dprintk("collect_cpu_topology(%d): %d\n", cpu, error);
	return error;
} /* collect_cpu_topology() */

static int collect_node_topology(int node)
{
	int error;
	struct ihk_node_topology *p = NULL;

	dprintk("collect_node_topology(%d)\n", node);
	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p) {
		error = -ENOMEM;
		eprintk("ihk:collect_node_topology:"
				"kzalloc failed. %d\n", error);
		goto out;
	}

	p->node_number = node;

	error = read_bitmap(&p->cpumap, nr_cpumask_bits,
			"/sys/devices/system/node/node%d/cpumap", node);
	if (error) {
		eprintk("ihk:collect_node_topology:"
				"read_bitmap failed. %d\n", error);
		// goto out;
	}

	error = 0;
	list_add(&p->chain, &node_topology_list);
	p = NULL;

out:
	kfree(p);
	dprintk("collect_node_topology(%d): %d\n", node, error);
	return error;
} /* collect_node_topology() */

int collect_topology(void)
{
	int error;
	int cpu;
	int node;

	dprintk("collect_topology()\n");
	for_each_cpu(cpu, cpu_online_mask) {
		error = collect_cpu_topology(cpu);
		if (error) {
			eprintk("ihk:collect_topology:"
					"collect_cpu_topology failed. %d\n",
					error);
			goto out;
		}
	}

	for_each_online_node(node) {
		error = collect_node_topology(node);
		if (error) {
			eprintk("ihk:collect_topology:"
					"collect_node_topology failed. %d\n",
					error);
			goto out;
		}
	}

	error = 0;
out:
	dprintk("collect_topology(): %d\n", error);
	return error;
} /* collect_topology() */

int smp_ihk_os_check_ikc_map(ihk_os_t ihk_os)
{
#ifdef IHK_IKC_USE_LINUX_WORK_IRQ
	return 0;
#else
	int i = 0, j = 0, cpu_count = 0, ret = 0, min = INT_MAX;
	uint8_t checkers[SMP_MAX_CPUS];

	memset(checkers, 0, sizeof(checkers));
	for (i = 0; i < SMP_MAX_CPUS; i++) {
		if ((ihk_smp_cpus[i].status != IHK_SMP_CPU_ASSIGNED) ||
		    (ihk_smp_cpus[i].os != ihk_os) ||
		    (checkers[i] == 1)) {
			continue;
		}

		for (j = 0; j < SMP_MAX_CPUS; j++) {
			if ((ihk_smp_cpus[j].status != IHK_SMP_CPU_ASSIGNED) ||
			    (ihk_smp_cpus[j].os != ihk_os)) {
				continue;
			}

			if (ihk_smp_cpus[i].ikc_map_cpu == ihk_smp_cpus[j].ikc_map_cpu) {
				checkers[j] = 1;
			}
		}

		if (ihk_smp_cpus[i].ikc_map_cpu < min) {
			min = ihk_smp_cpus[i].ikc_map_cpu;
		}
		cpu_count++;
		dprintk("%s: ihk_smp_cpus[%d].ikc_map_cpu=%d\n",
			__FUNCTION__, i, ihk_smp_cpus[i].ikc_map_cpu);
	}

	if (min != 0) {
		dprintk("%s: min is not 0.\n", __FUNCTION__);
		cpu_count++;
	}

	dprintk("%s: ihk_nr_irq=%d, cpu_count=%d\n", __FUNCTION__, ihk_nr_irq, cpu_count);
	if (ihk_nr_irq < cpu_count) {
		ret = 1;
		pr_warn("%s: ikc_map sections over ihk_nr_irqs, using default setting.\n",
			__func__);
	}
	return ret;
#endif
}

int ihk_smp_reset_cpu(int hw_id)
{
	int ret = 0;
	int i;
	u64 affi;

	dprintk(KERN_INFO "IHK-SMP: resetting CPU %d.\n", hw_id);

	if (!ihk_psci_ops->affinity_info) {
		pr_warn("IHK-SMP: Undefined reference to 'affinity_info'\n");
		return -EFAULT;
	}

	/* cpu_kill could race with cpu_die and we can
	 * potentially end up declaring this cpu undead
	 * while it is dying. So, try again a few times. */
	ret = ihk_smp_get_cpu_affinity(hw_id, &affi);
	if (ret) {
		pr_warn("IHK-SMP: ihk_smp_get_cpu_affinity failed.(ret=%d)\n", ret);
		return ret;
	}

	for (i = 0; i < SMP_MAX_CPUS; i++) {
		if ((ihk_smp_cpus[i].hw_id != hw_id) ||
		    (ihk_smp_cpus[i].status != IHK_SMP_CPU_ASSIGNED))
			continue;

		ret = ihk_psci_ops->affinity_info(affi, 0);
		if (ret == PSCI_0_2_AFFINITY_LEVEL_ON) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,1,0)
			ihk___smp_cross_call(cpumask_of(hw_id), INTRID_CPU_STOP);
#else
			ihk___smp_cross_call(&cpumask_of_cpu(hw_id), INTRID_CPU_STOP);
#endif
		}

		ret = ihk_smp_cpu_kill(hw_id, affi);
		break;
	}

	return ret;
}

void smp_ihk_arch_exit(void)
{
#ifndef IHK_IKC_USE_LINUX_WORK_IRQ
	int i = 0;
#endif // !IHK_IKC_USE_LINUX_WORK_IRQ

#if LINUX_VERSION_CODE <= KERNEL_VERSION(4, 3, 0)
	if (this_module_put) {
		try_module_get(THIS_MODULE);
	}
#endif

#ifndef IHK_IKC_USE_LINUX_WORK_IRQ
	for (i = 0; i < ihk_nr_irq; i++) {
		ihk_smp_release_irq(&ihk_smp_irq[i]);
	}
#endif // !IHK_IKC_USE_LINUX_WORK_IRQ

	if (trampoline_page) {
		int order = get_order(IHK_SMP_TRAMPOLINE_SIZE);
		free_pages((unsigned long)pfn_to_kaddr(page_to_pfn(trampoline_page)),
		           order);
	}
	else {
		iounmap(trampoline_va);
	}
	
	if (ident_npages_order) {
		free_pages((unsigned long)ident_page_table_virt,
		           ident_npages_order);
	}
}

#ifdef ENABLE_PERF
int smp_ihk_arch_get_perf_event_map(struct smp_boot_param *param)
{
	return 0;
}
#else /* ENABLE_PERF */
int smp_ihk_arch_get_perf_event_map(struct smp_boot_param *param)
{
	return 0;
}
#endif /* ENABLE_PERF */

void ihk_smp_free_page_tables(pgd_t *pt)
{
	printk(KERN_WARNING "%s: function not implemented.\n", __FUNCTION__);
}

int ihk_smp_map_kernel(pgd_t *pt, unsigned long vaddr, phys_addr_t paddr)
{
	printk(KERN_WARNING "%s: function not implemented.\n", __FUNCTION__);
	return 0;
}

int ihk_smp_print_pte(struct mm_struct *mm, unsigned long address)
{
	printk(KERN_WARNING "%s: function not implemented.\n", __FUNCTION__);
	return 0;
}
