/* smp-driver.h COPYRIGHT FUJITSU LIMITED 2015-2019 */
/**
 * \file smp-x86-driver.c
 * \brief
 *	IHK SMP-x86 Driver: IHK Host Driver
 *                        for partitioning an x86 SMP chip
 * \author Balazs Gerofi <bgerofi@is.s.u-tokyo.ac.jp> \par
 * Copyright (C) 2014 Balazs Gerofi <bgerofi@is.s.u-tokyo.ac.jp>
 *
 * Code partially based on IHK Builtin driver written by
 * Taku SHIMOSAWA <shimosawa@is.s.u-tokyo.ac.jp>
 */
#ifndef HEADER_SMP_SMP_DRIVER_H
#define HEADER_SMP_SMP_DRIVER_H

#include <linux/fs.h>
#include <linux/limits.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/version.h>
#include <ihk/ihk_host_driver.h>
#include <bootparam.h>

#ifdef IHK_DEBUG
#define dprintk(...) do { if (1) { printk(KERN_DEBUG __VA_ARGS__); } } while (0)
#define eprintk(...) do { if (1) { printk(KERN_ERR __VA_ARGS__); } } while (0)
#else
#define dprintk(...) do { if (0) { printk(KERN_DEBUG __VA_ARGS__); } } while (0)
#define eprintk(...) do { if (1) { printk(KERN_ERR __VA_ARGS__); } } while (0)
#endif

#define ARCHDRV_CHKANDJUMP(cond, msg, err)								\
	do {																\
		if(cond) {														\
			eprintk("%s:%d,"msg"\n", __FUNCTION__, __LINE__);					\
			ret = err;													\
			goto fn_fail;												\
		}																\
	} while(0)

#define BUILTIN_OS_STATUS_INITIAL	0
#define BUILTIN_OS_STATUS_LOADING	1
#define BUILTIN_OS_STATUS_LOADED	2
#define BUILTIN_OS_STATUS_BOOTING	3
#define BUILTIN_OS_STATUS_SHUTDOWN	4 /* shutting-down */
#define BUILTIN_OS_STATUS_HUNGUP	5

#define IHK_SMP_CPU_NONE	0
#define IHK_SMP_CPU_ONLINE	1
#define IHK_SMP_CPU_AVAILABLE	2
#define IHK_SMP_CPU_ASSIGNED	3
#define IHK_SMP_CPU_TO_OFFLINE	4
#define IHK_SMP_CPU_OFFLINED	5
#define IHK_SMP_CPU_TO_ONLINE	6

struct ihk_smp_cpu {
	int id;
	int hw_id;
	int status;
	ihk_os_t os;
	int ikc_map_cpu;
};

/** \brief BUILTIN driver-specific OS structure */
struct smp_os_data {
	/** \brief Lock for this structure */
	spinlock_t lock;

	/** \brief Pointer to the device structure */
	struct builtin_device_data *dev;
	/** \brief Allocated CPU core mask */
	struct smp_coreset cpu_hw_ids_map;
	/** \brief Start address of the allocated memory region */
	unsigned long mem_start;
	/** \brief End address of the allocated memory region */
	unsigned long mem_end;
	/** \brief Bitmask of NUMA nodes from where memory or
	 * CPUs are assigned */

	/* Memory chunk for kernel image and bootstrap page table */
	unsigned long bootstrap_mem_start, bootstrap_mem_end; 
	int bootstrap_numa_id;

	unsigned long numa_mask;

	/** \brief hardware ID of the bsp of this OS instance */
	int boot_cpu;
	/** \brief Entry point address of this OS instance */
	unsigned long boot_rip;
	pgd_t *boot_pt;

	/** \brief IHK Memory information */
	struct ihk_mem_info mem_info;
	/** \brief IHK Memory region information */
	struct ihk_mem_region mem_region;
	/** \brief IHK CPU information */
	struct ihk_cpu_info cpu_info;
	/** \brief hardware ID map of the CPU cores */
	int cpu_hw_ids[SMP_MAX_CPUS];

	/** \brief Kernel command-line parameter.
	 *
	 * This will be copied to boot_param just before booting so that
	 * it does not change while the kernel is running.
	 */
	char kernel_args[256];

	/* LWK NUMA id to Linux NUMA id mapping */
	int *numa_mapping;
	int nr_numa_nodes;

	/* LWK CPU id to Linux CPU id mapping */
	int cpu_mapping[SMP_MAX_CPUS];
	/* LWK CPU to Linux CPU mapping for IKC IRQ */
	int cpu_ikc_map[SMP_MAX_CPUS];
	int cpu_ikc_mapped;
	int nr_cpus;

	/** \brief Boot parameter for the kernel
	 *
	 * This structure is directly accessed (read and written)
	 * by the manycore kernel. */
	struct smp_boot_param *param;
	int param_pages_order;

	/** \brief Status of the kernel */
	int status;
};

/* ihk_os_mem_chunk represents a memory range which is used by
 * one of the OSs */
struct ihk_os_mem_chunk {
	struct list_head list;
	uintptr_t addr;
	size_t size;
	ihk_os_t os;
	int numa_id;
};

extern struct ihk_smp_cpu ihk_smp_cpus[SMP_MAX_CPUS];
extern unsigned long trampoline_phys;

extern unsigned long ident_page_table;

void *ihk_smp_map_virtual(unsigned long phys, unsigned long size);
void ihk_smp_unmap_virtual(void *virt);
int ihk_smp_set_multi_intr_mode(ihk_os_t ihk_os, void *priv, int mode);
int ihk_smp_set_nmi_mode(ihk_os_t ihk_os, void *priv, int mode);
irqreturn_t smp_ihk_irq_call_handlers(int irq, void *data);
int ihk_smp_map_kernel(pgd_t *pt, unsigned long vaddr, phys_addr_t paddr);
void smp_ihk_arch_dcache_flush(void *addr, size_t len);

int read_file(void *buf, size_t size, char *fmt, va_list ap);
int file_readable(char *fmt, ...);
int read_long(long *valuep, char *fmt, ...);
int read_bitmap(void *map, int nbits, char *fmt, ...);
int read_string(char **valuep, char *fmt, ...);

extern struct list_head cpu_topology_list;
extern struct list_head node_topology_list;

extern struct rb_root *ihk_vmap_area_root;

#ifdef IHK_IKC_USE_LINUX_WORK_IRQ
void smp_ihk_ikc_irq_work_func(struct irq_work *work);
extern struct llist_head *ihk__raised_list;
#endif // IHK_IKC_USE_LINUX_WORK_IRQ

#endif /* HEADER_SMP_SMP_DRIVER_H */
