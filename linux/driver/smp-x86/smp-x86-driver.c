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
#include <config.h>
#include <linux/string.h>
#include <linux/version.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/fs.h>
#include <asm/segment.h>
#include <asm/uaccess.h>
#include <linux/buffer_head.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/interrupt.h>
#include <linux/file.h>
#include <linux/elf.h>
#include <linux/cpu.h>
#include <linux/radix-tree.h>
#include <linux/rbtree.h>
#include <linux/irq.h>
#include <linux/topology.h>
#include <linux/ctype.h>
#include <linux/kallsyms.h>
#include <linux/list_sort.h>
#include <linux/swap.h>
#include <asm/hw_irq.h>
#if LINUX_VERSION_CODE == KERNEL_VERSION(2,6,32)
#include <linux/autoconf.h>
#endif
#include <ihk/ihk_host_driver.h>
#include <ihk/ihk_host_misc.h>
#include <ihk/ihk_host_user.h>
//#define IHK_DEBUG
#include <ihk/misc/debug.h>
#include <ikc/msg.h>
//#include <linux/shimos.h>
//#include "builtin_dma.h"
#include <asm/apic.h>
#include <asm/ipi.h>
#include <asm/uv/uv.h>
#include <asm/nmi.h>
#include <asm/tlbflush.h>
#include <asm/mc146818rtc.h>
#if defined(RHEL_RELEASE_CODE) || (LINUX_VERSION_CODE < KERNEL_VERSION(4,0,0))
#include <asm/smpboot_hooks.h>
#endif

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,38)
#include <asm/trampoline.h>
#elif (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,38)) && (LINUX_VERSION_CODE < KERNEL_VERSION(3,4,0))
#include <asm/trampoline.h>
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,0)
#include <asm/realmode.h>
#endif

#include "../../../cokernel/smp/x86/bootparam.h"

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

#define BUILTIN_OS_STATUS_INITIAL  0
#define BUILTIN_OS_STATUS_LOADING  1
#define BUILTIN_OS_STATUS_LOADED   2
#define BUILTIN_OS_STATUS_BOOTING  3
#define BUILTIN_OS_STATUS_SHUTDOWN  4 /* After shutdown */

#define BUILTIN_COM_VECTOR  0xf1

#define LARGE_PAGE_SIZE	(1UL << 21)
#define LARGE_PAGE_MASK	(~((unsigned long)LARGE_PAGE_SIZE - 1))

#define MAP_ST_START	0xffff800000000000UL
#define MAP_KERNEL_START	0xffffffff80000000UL

#define PTL4_SHIFT	39
#define PTL3_SHIFT	30
#define PTL2_SHIFT	21

#if defined(RHEL_RELEASE_CODE) || (LINUX_VERSION_CODE < KERNEL_VERSION(4,0,0))
#define BITMAP_SCNLISTPRINTF(buf, buflen, maskp, nmaskbits) \
	bitmap_scnlistprintf(buf, buflen, maskp, nmaskbits)
#else
#define BITMAP_SCNLISTPRINTF(buf, buflen, maskp, nmaskbits) \
	scnprintf(buf, buflen, "%*pbl", nmaskbits, maskp)
#endif


/*
 * IHK-SMP unexported kernel symbols
 */

/* x86_trampoline_base has been introduced in 2.6.38 */
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,38)) && (LINUX_VERSION_CODE < KERNEL_VERSION(3,4,0))
#ifdef IHK_KSYM_x86_trampoline_base
#if IHK_KSYM_x86_trampoline_base
unsigned char *x86_trampoline_base = 
	(void *)
	IHK_KSYM_x86_trampoline_base;
#endif
#endif
#endif

/* real_mode_header has been introduced in 3.10 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,0)
#ifdef IHK_KSYM_real_mode_header
#if IHK_KSYM_real_mode_header
struct real_mode_header *real_mode_header = 
	(void *)
	IHK_KSYM_real_mode_header;
#endif
#endif
#endif

#ifdef IHK_KSYM_per_cpu__vector_irq
#if IHK_KSYM_per_cpu__vector_irq
void *_per_cpu__vector_irq = 
	(void *)
	IHK_KSYM_per_cpu__vector_irq;
#endif
#endif

#ifdef IHK_KSYM_vector_irq
#if IHK_KSYM_vector_irq
void *_vector_irq = 
	(void *)
	IHK_KSYM_vector_irq;
#endif
#endif

#ifdef IHK_KSYM_lapic_get_maxlvt
#if IHK_KSYM_lapic_get_maxlvt
typedef int (*int_star_fn_void_t)(void); 
int (*_lapic_get_maxlvt)(void) = 
	(int_star_fn_void_t)
	IHK_KSYM_lapic_get_maxlvt;
#endif
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,3,0)
#ifdef IHK_KSYM_init_deasserted
#if IHK_KSYM_init_deasserted
atomic_t *_init_deasserted = 
	(atomic_t *)
	IHK_KSYM_init_deasserted;
#endif
#endif
#endif

#ifdef IHK_KSYM_irq_to_desc
#if IHK_KSYM_irq_to_desc
typedef struct irq_desc *(*irq_desc_star_fn_int_t)(unsigned int);
struct irq_desc *(*_irq_to_desc)(unsigned int irq) = 
	(irq_desc_star_fn_int_t)
	IHK_KSYM_irq_to_desc;
#else // exported
#include <linux/irqnr.h>
struct irq_desc *(*_irq_to_desc)(unsigned int irq) = irq_to_desc;
#endif
#endif

#ifdef IHK_KSYM_irq_to_desc_alloc_node
#if IHK_KSYM_irq_to_desc_alloc_node
typedef struct irq_desc *(*irq_desc_star_fn_int_int_t)(unsigned int, int);
struct irq_desc *(*_irq_to_desc_alloc_node)(unsigned int irq, int node) =
	(irq_desc_star_fn_int_int_t)
	IHK_KSYM_irq_to_desc_alloc_node;
#endif
#endif

#ifdef IHK_KSYM_alloc_desc
#if IHK_KSYM_alloc_desc
typedef struct irq_desc *(*irq_desc_star_fn_int_int_module_star_t)
	(int, int, struct module*);
struct irq_desc *(*_alloc_desc)(int irq, int node, struct module *owner) =
	(irq_desc_star_fn_int_int_module_star_t)
	IHK_KSYM_alloc_desc;
#endif
#endif

#ifdef IHK_KSYM_irq_desc_tree
#if IHK_KSYM_irq_desc_tree
struct radix_tree_root *_irq_desc_tree = 
	(struct radix_tree_root *)
	IHK_KSYM_irq_desc_tree;
#endif
#endif

#ifdef IHK_KSYM_dummy_irq_chip
#if IHK_KSYM_dummy_irq_chip
struct irq_chip *_dummy_irq_chip =
	(struct irq_chip *)
	IHK_KSYM_dummy_irq_chip;
#else // exported
struct irq_chip *_dummy_irq_chip = &dummy_irq_chip;
#endif
#endif

#ifdef IHK_KSYM_get_uv_system_type
#if IHK_KSYM_get_uv_system_type
typedef enum uv_system_type (*uv_system_type_star_fn_void_t)(void); 
enum uv_system_type (*_get_uv_system_type)(void) = 
	(uv_system_type_star_fn_void_t)
	IHK_KSYM_get_uv_system_type;
#endif
#else // static
#define _get_uv_system_type get_uv_system_type
#endif

#ifdef IHK_KSYM_wakeup_secondary_cpu_via_init
#if IHK_KSYM_wakeup_secondary_cpu_via_init
int (*_wakeup_secondary_cpu_via_init)(int phys_apicid, 
	unsigned long start_eip) = 
	IHK_KSYM_wakeup_secondary_cpu_via_init;
#endif
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,6,0)
#ifdef IHK_KSYM___default_send_IPI_dest_field
#if IHK_KSYM___default_send_IPI_dest_field
typedef void (*void_fn_unsigned_int_int_unsigned_int_t)(void);
void (*___default_send_IPI_dest_field)(unsigned int mask, 
	int vector, unsigned int dest)
	= (void_fn_unsigned_int_int_unsigned_int_t)
	IHK_KSYM___default_send_IPI_dest_field;
#endif
#endif
#endif

static unsigned long ihk_phys_start = 0;
module_param(ihk_phys_start, ulong, 0644);
MODULE_PARM_DESC(ihk_phys_start, "IHK reserved physical memory start address");

static unsigned long ihk_mem = 0;
module_param(ihk_mem, ulong, 0644);
MODULE_PARM_DESC(ihk_mem, "IHK reserved memory in MBs");

static unsigned int ihk_cores = 0;
module_param(ihk_cores, uint, 0644);
MODULE_PARM_DESC(ihk_cores, "IHK reserved CPU cores");

static unsigned int ihk_start_irq = 0;
module_param(ihk_start_irq, uint, 0644);
MODULE_PARM_DESC(ihk_start_irq, "IHK IKC IPI to be scanned from this IRQ vector");

static unsigned int ihk_ikc_irq_core = 0;
module_param(ihk_ikc_irq_core, uint, 0644);
MODULE_PARM_DESC(ihk_ikc_irq_core, "Target CPU of IHK IKC IRQ");

static unsigned long ihk_trampoline = 0;
module_param(ihk_trampoline, ulong, 0644);
MODULE_PARM_DESC(ihk_trampoline, "IHK trampoline page physical address");

#define IHK_SMP_CPU_ONLINE		0
#define IHK_SMP_CPU_AVAILABLE	1
#define IHK_SMP_CPU_ASSIGNED	2
#define IHK_SMP_CPU_TO_OFFLINE	3
#define IHK_SMP_CPU_OFFLINED	4
#define IHK_SMP_CPU_TO_ONLINE	5

struct ihk_smp_cpu {
	int id;
	int apic_id;
	int status;
	ihk_os_t os;
	int ikc_map_cpu;
};

static struct ihk_smp_cpu ihk_smp_cpus[SMP_MAX_CPUS];
struct page *trampoline_page;
unsigned long trampoline_phys;
int using_linux_trampoline = 0;
char linux_trampoline_backup[4096];
void *trampoline_va;

unsigned long ident_page_table;
int ident_npages_order = 0;
unsigned long *ident_page_table_virt;

int ihk_smp_irq = 0;
int ihk_smp_irq_apicid = 0;
int this_module_put = 0;
int ihk_smp_reset_cpu(int phys_apicid);

extern const char ihk_smp_trampoline_end[], ihk_smp_trampoline_data[];
#define IHK_SMP_TRAMPOLINE_SIZE \
	roundup(ihk_smp_trampoline_end - ihk_smp_trampoline_data, PAGE_SIZE)

/* ----------------------------------------------- */

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

	/** \brief APIC ID of the bsp of this OS instance */
	int boot_cpu;
	/** \brief Entry point address of this OS instance */
	unsigned long boot_rip;

	/** \brief IHK Memory information */
	struct ihk_mem_info mem_info;
	/** \brief IHK Memory region information */
	struct ihk_mem_region mem_region;
	/** \brief IHK CPU information */
	struct ihk_cpu_info cpu_info;
	/** \brief APIC ID map of the CPU cores */
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

#define BUILTIN_DEV_STATUS_READY    0
#define BUILTIN_DEV_STATUS_BOOTING  1

/** \brief Driver-speicific device structure
 *
 * This structure is very simple because it is assumed that there is only
 * one BUILTIN device (because it uses the host machine actually!) in a machine.
 */
struct builtin_device_data {
	spinlock_t lock;
	ihk_device_t ihk_dev;
	int status;

	struct ihk_dma_channel builtin_host_channel;
};

/* Chunk denotes a memory range that is pre-reserved by IHK-SMP.
 * NOTE: chunk structures reside at the beginning of the physical memory
 * they represent!! */
struct chunk {
	struct list_head chain;
	struct rb_node node;
	uintptr_t addr;
	size_t size;
	int numa_id;
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

static struct list_head ihk_mem_free_chunks;
static struct list_head ihk_mem_used_chunks;

static int smp_ihk_os_get_special_addr(ihk_os_t ihk_os, void *priv,
                                       enum ihk_special_addr_type type,
                                       unsigned long *addr,
                                       unsigned long *size);
static unsigned long smp_ihk_os_map_memory(ihk_os_t ihk_os, void *priv,
                                           unsigned long remote_phys,
                                           unsigned long size);
static void *smp_ihk_map_virtual(ihk_device_t ihk_dev, void *priv,
                                 unsigned long phys, unsigned long size,
                                 void *virt, int flags);
static int smp_ihk_unmap_virtual(ihk_device_t ihk_dev, void *priv,
                                  void *virt, unsigned long size);
static int smp_ihk_unmap_memory(ihk_device_t ihk_dev, void *priv,
                                unsigned long local_phys,
                                unsigned long size);

#if defined(RHEL_RELEASE_CODE) || (LINUX_VERSION_CODE < KERNEL_VERSION(4,0,0))
#else
/* origin: arch/x86/kernel/smpboot.c */
static inline void smpboot_setup_warm_reset_vector(unsigned long start_eip)
{
	unsigned long flags;

	spin_lock_irqsave(&rtc_lock, flags);
	CMOS_WRITE(0xa, 0xf);
	spin_unlock_irqrestore(&rtc_lock, flags);
	local_flush_tlb();
	pr_debug("1.\n");
	*((volatile unsigned short *)phys_to_virt(TRAMPOLINE_PHYS_HIGH)) =
							start_eip >> 4;
	pr_debug("2.\n");
	*((volatile unsigned short *)phys_to_virt(TRAMPOLINE_PHYS_LOW)) =
							start_eip & 0xf;
	pr_debug("3.\n");
}
#if LINUX_VERSION_CODE > KERNEL_VERSION(4,3,5)
#warning smpboot_setup_warm_reset_vector() has been only tested up to 4.3.0 kernels
#endif
#endif

void *ihk_smp_map_virtual(unsigned long phys, unsigned long size)
{
	struct ihk_os_mem_chunk *os_mem_chunk = NULL;

	/* look up address among used chunks */
	list_for_each_entry(os_mem_chunk, &ihk_mem_used_chunks, list) {
		if (phys >= os_mem_chunk->addr && 
				(phys + size) <= (os_mem_chunk->addr + 
					os_mem_chunk->size)) {

			return (phys_to_virt(os_mem_chunk->addr) 
					+ (phys - os_mem_chunk->addr));
		}
	}

	return 0;
}

void ihk_smp_unmap_virtual(void *virt)
{
	/* TODO: look up chunks and report error if not in range */
	return;	
}

/** \brief Implementation of ihk_host_get_dma_channel.
 *
 * It returns the information of the only channel in the DMA emulating core. */
static ihk_dma_channel_t smp_ihk_get_dma_channel(ihk_device_t dev, void *priv,
                                                 int channel)
{
	return NULL;
}

/** \brief Set the status member of the OS data with lock */
static void set_os_status(struct smp_os_data *os, int status)
{
	unsigned long flags;

	spin_lock_irqsave(&os->lock, flags);
	os->status = status;
	spin_unlock_irqrestore(&os->lock, flags);
}

/** \brief Set the status member of the OS data with lock */
static void set_dev_status(struct builtin_device_data *dev, int status)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->lock, flags);
	dev->status = status;
	spin_unlock_irqrestore(&dev->lock, flags);
}

/** \brief Create various information structure that should be provided
 * via IHK functions. */
static void __build_os_info(struct smp_os_data *os)
{
	os->mem_info.n_mappable = os->mem_info.n_available = 1;
	os->mem_info.n_fixed = 0;
	os->mem_info.available = os->mem_info.mappable = &os->mem_region;
	os->mem_info.fixed = NULL;
	os->mem_region.start = os->mem_start;
	os->mem_region.size = os->mem_end - os->mem_start;

	os->mem_info.n_numa_nodes = os->nr_numa_nodes;
	os->mem_info.numa_mapping = os->numa_mapping;

	os->cpu_info.n_cpus = os->nr_cpus;
	os->cpu_info.mapping = os->cpu_mapping;
	os->cpu_info.hw_ids = os->cpu_hw_ids;
	os->cpu_info.ikc_map = os->cpu_ikc_map;
	os->cpu_info.ikc_mapped = os->cpu_ikc_mapped;
}

struct ihk_smp_trampoline_header {
	unsigned long reserved;   /* jmp ins. */
	unsigned long page_table; /* ident page table */
	unsigned long next_ip;    /* the program address */
	unsigned long stack_ptr;  /* stack pointer */
	unsigned long notify_address; /* notification address */
};

static int smp_wakeup_secondary_cpu_via_init(int phys_apicid, 
		unsigned long start_eip)
{
	unsigned long send_status, accept_status = 0;
	int maxlvt, num_starts, j;

	maxlvt = _lapic_get_maxlvt();

	/*
	 * Be paranoid about clearing APIC errors.
	 */
	if (APIC_INTEGRATED(apic_version[phys_apicid])) {
		if (maxlvt > 3)		/* Due to the Pentium erratum 3AP.  */
			apic_write(APIC_ESR, 0);
		apic_read(APIC_ESR);
	}

	pr_debug("Asserting INIT.\n");

	/*
	 * Turn INIT on target chip
	 */
	/*
	 * Send IPI
	 */
	apic_icr_write(APIC_INT_LEVELTRIG | APIC_INT_ASSERT | APIC_DM_INIT,
		       phys_apicid);

	pr_debug("Waiting for send to finish...\n");
	send_status = safe_apic_wait_icr_idle();

	mdelay(10);

	pr_debug("Deasserting INIT.\n");

	/* Target chip */
	/* Send IPI */
	apic_icr_write(APIC_INT_LEVELTRIG | APIC_DM_INIT, phys_apicid);

	pr_debug("Waiting for send to finish...\n");
	send_status = safe_apic_wait_icr_idle();

	mb();
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,3,0)
	atomic_set(_init_deasserted, 1);
#endif

	/*
	 * Should we send STARTUP IPIs ?
	 *
	 * Determine this based on the APIC version.
	 * If we don't have an integrated APIC, don't send the STARTUP IPIs.
	 */
	if (APIC_INTEGRATED(apic_version[phys_apicid]))
		num_starts = 2;
	else
		num_starts = 0;

	/*
	 * Run STARTUP IPI loop.
	 */
	pr_debug("#startup loops: %d.\n", num_starts);

	for (j = 1; j <= num_starts; j++) {
		pr_debug("Sending STARTUP #%d.\n", j);
		if (maxlvt > 3)		/* Due to the Pentium erratum 3AP.  */
			apic_write(APIC_ESR, 0);
		apic_read(APIC_ESR);
		pr_debug("After apic_write.\n");

		/*
		 * STARTUP IPI
		 */

		/* Target chip */
		/* Boot on the stack */
		/* Kick the second */
		apic_icr_write(APIC_DM_STARTUP | (start_eip >> 12),
			       phys_apicid);

		/*
		 * Give the other CPU some time to accept the IPI.
		 */
		udelay(300);

		pr_debug("Startup point 1.\n");

		pr_debug("Waiting for send to finish...\n");
		send_status = safe_apic_wait_icr_idle();

		/*
		 * Give the other CPU some time to accept the IPI.
		 */
		udelay(200);
		if (maxlvt > 3)		/* Due to the Pentium erratum 3AP.  */
			apic_write(APIC_ESR, 0);
		accept_status = (apic_read(APIC_ESR) & 0xEF);
		if (send_status || accept_status)
			break;
	}
	pr_debug("After Startup.\n");

	if (send_status)
		printk(KERN_ERR "APIC never delivered???\n");
	if (accept_status)
		printk(KERN_ERR "APIC delivery error (%lx).\n", accept_status);

	return (send_status | accept_status);
}

int smp_wakeup_secondary_cpu(int apicid, unsigned long start_eip)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,3,0)
	atomic_set(_init_deasserted, 0);
#endif

	if (_get_uv_system_type() != UV_NON_UNIQUE_APIC) {

		pr_debug("Setting warm reset code and vector.\n");

		smpboot_setup_warm_reset_vector(start_eip);
		/*
		 * Be paranoid about clearing APIC errors.
		 */
		if (APIC_INTEGRATED(apic_version[boot_cpu_physical_apicid])) {
			apic_write(APIC_ESR, 0);
			apic_read(APIC_ESR);
		}
	}

	if (apic->wakeup_secondary_cpu) {
		printk("%s: apic->wakeup_secondary_cpu()\n", __FUNCTION__);
		return apic->wakeup_secondary_cpu(apicid, start_eip);
	}
	else {
		int ret;
		printk("%s: smp_wakeup_secondary_cpu_via_init()\n", __FUNCTION__);

		preempt_disable();
		ret = smp_wakeup_secondary_cpu_via_init(apicid, start_eip);
		preempt_enable();

		return ret;
	}
}

unsigned long x2apic_is_enabled(void)
{
	unsigned long msr;

	rdmsrl(MSR_IA32_APICBASE, msr);

	return msr & (1 << 10); /* x2APIC enabled? */
}

/*
 * CPU and NUMA node mapping conversion functions.
 */
static int lwk_cpu_2_linux_cpu(struct smp_os_data *os, int cpu_id)
{
	return (cpu_id < os->nr_cpus) ? os->cpu_mapping[cpu_id] : -1;
}

static int linux_cpu_2_lwk_cpu(struct smp_os_data *os, int cpu_id)
{
	int i;

	for (i = 0; i < os->nr_cpus; ++i) {
		if (os->cpu_mapping[i] == cpu_id)
			return i;
	}

	return -1;
}

static int lwk_numa_2_linux_numa(struct smp_os_data *os, int numa_id)
{
	return (numa_id < os->nr_numa_nodes) ?
		os->numa_mapping[numa_id] : -1;
}

static int linux_numa_2_lwk_numa(struct smp_os_data *os, int numa_id)
{
	int i;

	for (i = 0; i < os->nr_numa_nodes; ++i) {
		if (os->numa_mapping[i] == numa_id)
			return i;
	}

	return -1;
}


/** \brief Boot a kernel. */
static int smp_ihk_os_boot(ihk_os_t ihk_os, void *priv, int flag)
{
	struct smp_os_data *os = priv;
	struct builtin_device_data *dev = os->dev;
	unsigned long flags;
	struct ihk_smp_trampoline_header *header;
	struct timespec now;
	int param_size, param_pages_order = 0;
	struct page *param_pages;
	struct ihk_os_mem_chunk *os_mem_chunk;
	int nr_memory_chunks = 0;
	int numa_id, linux_numa_id, nr_numa_nodes;
	struct ihk_smp_boot_param_cpu *bp_cpu;
	struct ihk_smp_boot_param_numa_node *bp_numa_node;
	struct ihk_smp_boot_param_memory_chunk *bp_mem_chunk;
	int lwk_cpu;
	int *ihk_smp_boot_numa_distance;
	int i, j;

	/* Compute size including CPUs, NUMA nodes and memory chunks */
	param_size = (sizeof(*os->param));
	param_size += os->nr_cpus * sizeof(struct ihk_smp_boot_param_cpu);

	/* NUMA nodes */
	nr_numa_nodes = hweight64(os->numa_mask);
	param_size += (nr_numa_nodes * sizeof(struct ihk_smp_boot_param_numa_node));

	/* NUMA distances */
	param_size += nr_numa_nodes * nr_numa_nodes * sizeof(int);

	os->numa_mapping = kmalloc(nr_numa_nodes * sizeof(int), GFP_KERNEL);
	if (!os->numa_mapping) {
		printk("IHK-SMP: error allocating NUMA mapping\n");
		return -1;
	}

	/* Fill in LWK NUMA mapping */
	numa_id = 0;
	for (linux_numa_id = find_first_bit(&os->numa_mask,
				(sizeof(os->numa_mask) * 8));
			linux_numa_id < (sizeof(os->numa_mask) * 8);
			linux_numa_id = find_next_bit(&os->numa_mask,
				(sizeof(os->numa_mask) * 8), linux_numa_id + 1)) {

		os->numa_mapping[numa_id] = linux_numa_id;
		dprintf("IHK-SMP: OS: %p, NUMA: %d => Linux NUMA: %d\n",
				os, numa_id, linux_numa_id);

		++numa_id;
	}

	/* Count number of memory chunks */
	list_for_each_entry(os_mem_chunk, &ihk_mem_used_chunks, list) {
		if (os_mem_chunk->os != ihk_os)
			continue;
		++nr_memory_chunks;
	}

	param_size += (nr_memory_chunks *
			sizeof(struct ihk_smp_boot_param_memory_chunk));

	dprintf("IHK-SMP: %d memory chunks from %d NUMA nodes\n",
		nr_memory_chunks, nr_numa_nodes);

	/* Allocate boot parameter pages */
	param_size = (param_size + PAGE_SIZE - 1) & PAGE_MASK;
	param_pages_order = 0;
	while (((size_t)PAGE_SIZE << param_pages_order) < param_size)
		++param_pages_order;

	param_pages = alloc_pages(GFP_KERNEL | __GFP_ZERO, param_pages_order);
	if (!param_pages) {
		kfree(os);
		printk("IHK-SMP: error: allocating boot parameter structure\n");
		return -ENOMEM;
	}

	os->param = pfn_to_kaddr(page_to_pfn(param_pages));
	os->param->param_size = param_size;
	os->param_pages_order = param_pages_order;
	printk("IHK-SMP: boot param size: %d, nr_pages: %lu\n",
			param_size, 1UL << param_pages_order);

	os->param->nr_cpus = os->nr_cpus;
	os->param->nr_linux_cpus = nr_cpu_ids;
	os->param->nr_numa_nodes = nr_numa_nodes;
	os->param->nr_memory_chunks = nr_memory_chunks;

	os->nr_numa_nodes = nr_numa_nodes;

	bp_cpu = (struct ihk_smp_boot_param_cpu *)((char *)os->param +
			sizeof(*os->param));

	/* NOTE: CPU mapping is determined by the CPU assign operation
	 * so that the order can be controlled by the user */
	/* Pass in CPU information according to CPU mapping */
	for (lwk_cpu = 0; lwk_cpu < os->nr_cpus; ++lwk_cpu) {
		bp_cpu->numa_id = linux_numa_2_lwk_numa(os,
				cpu_to_node(os->cpu_mapping[lwk_cpu]));
		bp_cpu->hw_id = os->cpu_hw_ids[lwk_cpu];
		bp_cpu->linux_cpu_id = os->cpu_mapping[lwk_cpu];
		bp_cpu->ikc_cpu = ihk_smp_cpus[lwk_cpu_2_linux_cpu(os, lwk_cpu)].ikc_map_cpu;
		os->cpu_ikc_map[lwk_cpu] = bp_cpu->ikc_cpu;

		dprintf("IHK-SMP: OS: %p, Linux NUMA: %d, LWK CPU: %d,"
				" CPU APIC: %d, IKC CPU: %d\n",
				os, cpu_to_node(os->cpu_mapping[lwk_cpu]), lwk_cpu,
				bp_cpu->hw_id, bp_cpu->ikc_cpu);

		++bp_cpu;
	}

	bp_numa_node = (struct ihk_smp_boot_param_numa_node *)bp_cpu;

	/* Fill in NUMA nodes information */
	numa_id = 0;
	for (linux_numa_id = find_first_bit(&os->numa_mask,
				(sizeof(os->numa_mask) * 8));
			linux_numa_id < (sizeof(os->numa_mask) * 8);
			linux_numa_id = find_next_bit(&os->numa_mask,
				(sizeof(os->numa_mask) * 8), linux_numa_id + 1)) {

		bp_numa_node->type = IHK_SMP_MEMORY_TYPE_DRAM;
		bp_numa_node->linux_numa_id = linux_numa_id;

		dprintf("IHK-SMP: OS: %p, NUMA: %d => Linux NUMA: %d\n",
				os, numa_id, linux_numa_id);

		++bp_numa_node;
		++numa_id;
	}

	bp_mem_chunk = (struct ihk_smp_boot_param_memory_chunk *)bp_numa_node;

	/* Fill in memory chunks information in the order of NUMA nodes */
	for (linux_numa_id = find_first_bit(&os->numa_mask,
				(sizeof(os->numa_mask) * 8));
			linux_numa_id < (sizeof(os->numa_mask) * 8);
			linux_numa_id = find_next_bit(&os->numa_mask,
				(sizeof(os->numa_mask) * 8), linux_numa_id + 1)) {

		list_for_each_entry(os_mem_chunk, &ihk_mem_used_chunks, list) {
			if (os_mem_chunk->os != ihk_os ||
					os_mem_chunk->numa_id != linux_numa_id)
				continue;

			bp_mem_chunk->start = os_mem_chunk->addr;
			bp_mem_chunk->end = os_mem_chunk->addr + os_mem_chunk->size;
			bp_mem_chunk->numa_id =
				linux_numa_2_lwk_numa(os, os_mem_chunk->numa_id);

			++bp_mem_chunk;
		}
	}

	/* Fill in NUMA distances */
	ihk_smp_boot_numa_distance = (int *)bp_mem_chunk;
	for (i = 0; i < nr_numa_nodes; ++i) {
		for (j = 0; j < nr_numa_nodes; ++j) {
			*ihk_smp_boot_numa_distance =
				node_distance(
						lwk_numa_2_linux_numa(os, i),
						lwk_numa_2_linux_numa(os, j));
			++ihk_smp_boot_numa_distance;
		}
	}

	spin_lock_irqsave(&dev->lock, flags);
#if 0
	if (dev->status != BUILTIN_DEV_STATUS_READY) {
		spin_unlock_irqrestore(&dev->lock, flags);
		printk("builtin: Device is busy booting another OS.\n");
		return -EINVAL;
	}
#endif
	dev->status = BUILTIN_DEV_STATUS_BOOTING;
	spin_unlock_irqrestore(&dev->lock, flags);
	
	__build_os_info(os);
	if (os->cpu_info.n_cpus < 1) {
		dprintf("builtin: There are no CPU to boot!\n");
		set_dev_status(dev, BUILTIN_DEV_STATUS_READY);

		return -EINVAL;
	}
	os->boot_cpu = os->cpu_info.hw_ids[0];

	if(os->status == BUILTIN_OS_STATUS_BOOTING) {
		printk("IHK: Device is busy booting another OS.\n");
		return -EINVAL;
	}

	set_os_status(os, BUILTIN_OS_STATUS_BOOTING);

	dprint_var_x4(os->boot_cpu);
	dprint_var_x8(os->boot_rip);

	os->param->start = os->mem_start;
	os->param->end = os->mem_end;
	os->param->bootstrap_mem_end = os->bootstrap_mem_end;
	os->param->ident_table = ident_page_table;
	strncpy(os->param->kernel_args, os->kernel_args,
	        sizeof(os->param->kernel_args));

	os->param->ns_per_tsc = 1000000000L / tsc_khz;
	getnstimeofday(&now);
	os->param->boot_sec = now.tv_sec;
	os->param->boot_nsec = now.tv_nsec;
	os->param->ihk_ikc_irq = ihk_smp_irq;
	for (i = 0; i < nr_cpu_ids; i++) {
		os->param->ihk_ikc_irq_apicids[i] = per_cpu(x86_bios_cpu_apicid, i);
	}

	dprintf("boot cpu : %d, %lx, %lx, %lx, %lx\n",
	        os->boot_cpu, os->mem_start, os->mem_end, os->cpu_hw_ids_map.set[0],
	        os->param->dma_address
	);

	/* Make a temporary copy of the Linux trampoline */
	if (using_linux_trampoline) {
		memcpy(linux_trampoline_backup, trampoline_va, IHK_SMP_TRAMPOLINE_SIZE);
	}

	/* Prepare trampoline code */
	memcpy(trampoline_va, ihk_smp_trampoline_data, IHK_SMP_TRAMPOLINE_SIZE);

	header = trampoline_va; 
	header->page_table = ident_page_table;
	header->next_ip = os->boot_rip;
	header->notify_address = __pa(os->param);
	
	printk("IHK-SMP: booting OS 0x%lx, calling smp_wakeup_secondary_cpu() \n", 
		(unsigned long)ihk_os);
	udelay(300);
	
	return smp_wakeup_secondary_cpu(os->boot_cpu, trampoline_phys);
	
	/* Never reach these.. */
	linux_numa_2_lwk_numa(os, 0);
	linux_cpu_2_lwk_cpu(os, 0);
	lwk_numa_2_linux_numa(os, 0);
	lwk_cpu_2_linux_cpu(os, 0);
}

static int smp_ihk_os_load_file(ihk_os_t ihk_os, void *priv, const char *fn)
{
	struct smp_os_data *os = priv;
	struct file *file;
	loff_t pos = 0;
	long r;
	mm_segment_t fs;
	unsigned long phys;
	unsigned long offset;
	unsigned long maxoffset;
	unsigned long flags;
	Elf64_Ehdr *elf64;
	Elf64_Phdr *elf64p;
	int i;
	unsigned long entry;
	unsigned long pml4_p;
	unsigned long pdp_p;
	unsigned long pde_p;
	unsigned long *pml4;
	unsigned long *pdp;
	unsigned long *pde;
	unsigned long *cr3;
	int n;
	extern char startup_data[];
	extern char startup_data_end[];
	unsigned long startup_p;
	unsigned long *startup;
	struct ihk_os_mem_chunk *os_mem_chunk_iter;
	struct ihk_os_mem_chunk *os_mem_chunk = NULL;
	os->bootstrap_mem_start = 0;
	os->bootstrap_mem_end = 0;

	/* Update bootstrap_numa_id with the lowest NUMA id if not set */
	/* TODO: add IHK API to set bootstrap_numa_id */
	if (os->bootstrap_numa_id == -1) {
		int min_numa_id = -1;

		list_for_each_entry(os_mem_chunk_iter, &ihk_mem_used_chunks, list) {
			if (min_numa_id != -1 &&
					min_numa_id <= os_mem_chunk_iter->numa_id) {
				continue;
			}

			min_numa_id = os_mem_chunk_iter->numa_id;
		}

		os->bootstrap_numa_id = min_numa_id;
	}

	/* Find the bootstrap memory chunk for image and page table */
	list_for_each_entry(os_mem_chunk_iter, &ihk_mem_used_chunks, list) {
		if (os_mem_chunk_iter->os != ihk_os ||
				os_mem_chunk_iter->numa_id != os->bootstrap_numa_id) {
			continue;
		}

		/* Find the largest memory chunk on the bootstrap NUMA node */
		if ((os->bootstrap_mem_end - os->bootstrap_mem_start) <
				os_mem_chunk_iter->size) {
			os_mem_chunk = os_mem_chunk_iter;
			os->bootstrap_mem_start = os_mem_chunk->addr;
			os->bootstrap_mem_end = os_mem_chunk->addr + os_mem_chunk->size;
		}
	}

	if (os_mem_chunk == NULL) {
		printk("%s: couldn't find NUMA node to load kernel image\n",
				__FUNCTION__);
		return -EINVAL;
	}

	printk("IHK-SMP: bootstrap addr: 0x%lx, chunk size: %lu @ NUMA: %d\n",
			os->bootstrap_mem_start,
			os->bootstrap_mem_end - os->bootstrap_mem_start,
			os->bootstrap_numa_id);

	if (!CORE_ISSET_ANY(&os->cpu_hw_ids_map) ||
			os->bootstrap_mem_end - os->bootstrap_mem_start < 0) {
		printk("%s: OS is not ready to boot\n", __FUNCTION__);
		return -EINVAL;
	}

	spin_lock_irqsave(&os->lock, flags);
	if (os->status != BUILTIN_OS_STATUS_INITIAL) {
		printk("builtin: OS status is not initial.\n");
		spin_unlock_irqrestore(&os->lock, flags);
		return -EBUSY;
	}
	os->status = BUILTIN_OS_STATUS_LOADING;
	spin_unlock_irqrestore(&os->lock, flags);

	file = filp_open(fn, O_RDONLY, 0);
	if (IS_ERR(file)) {
		printk("open failed: %s\n", fn);
		return -ENOENT;
	}

	elf64 = ihk_smp_map_virtual(os->bootstrap_mem_end - PAGE_SIZE, PAGE_SIZE);
	if (!elf64) {
		printk("error: ioremap() returns NULL\n");
		return -EINVAL;
	}

	fs = get_fs();
	set_fs(get_ds());
	printk("IHK-SMP: loading ELF header for OS 0x%lx, phys=0x%lx\n",
		(unsigned long)ihk_os, os->bootstrap_mem_end - PAGE_SIZE);

	r = vfs_read(file, (char *)elf64, PAGE_SIZE, &pos);
	set_fs(fs);
	if (r <= 0) {
		printk("vfs_read failed: %ld\n", r);
		ihk_smp_unmap_virtual(elf64);
		fput(file);
		return (int)r;
	}
	if(elf64->e_ident[0] != 0x7f ||
	   elf64->e_ident[1] != 'E' ||
	   elf64->e_ident[2] != 'L' ||
	   elf64->e_ident[3] != 'F' ||
	   elf64->e_phoff + sizeof(Elf64_Phdr) * elf64->e_phnum > PAGE_SIZE){
		printk("kernel: BAD ELF\n");
		ihk_smp_unmap_virtual(elf64);
		fput(file);
		return (int)-EINVAL;
	}
	entry = elf64->e_entry;
	elf64p = (Elf64_Phdr *)(((char *)elf64) + elf64->e_phoff);
	phys = (os->bootstrap_mem_start + LARGE_PAGE_SIZE * 2 - 1) & LARGE_PAGE_MASK;
	maxoffset = phys;

	for(i = 0; i < elf64->e_phnum; i++){
		unsigned long end;
		unsigned long size;
		char *buf;
		unsigned long pphys;
		unsigned long psize;

		if (elf64p[i].p_type != PT_LOAD)
			continue;
		if (elf64p[i].p_vaddr == 0)
			continue;

		offset = elf64p[i].p_vaddr - (MAP_KERNEL_START - phys);
		pphys = offset;
		psize = (elf64p[i].p_memsz + PAGE_SIZE - 1) & PAGE_MASK;
		size = elf64p[i].p_filesz;
		pos = elf64p[i].p_offset;
		end = pos + size;
		while(pos < end){
			long l = end - pos;

			if (l > PAGE_SIZE)
				l = PAGE_SIZE;

			if (offset + PAGE_SIZE > os->bootstrap_mem_end) {
				printk("builtin: OS is too big to load.\n");
				return -E2BIG;
			}

			buf = ihk_smp_map_virtual(offset, PAGE_SIZE);
			fs = get_fs();
			set_fs(get_ds());
			r = vfs_read(file, buf, l, &pos);
			set_fs(fs);
			if(r != PAGE_SIZE){
				memset(buf + r, '\0', PAGE_SIZE - r);
			}
			ihk_smp_unmap_virtual(buf);
			if (r <= 0) {
				printk("vfs_read failed: %ld\n", r);
				ihk_smp_unmap_virtual(elf64);
				fput(file);
				return (int)r;
			}
			offset += PAGE_SIZE;
		}

		for (size = (size + PAGE_SIZE - 1) & PAGE_MASK;
				size < psize; size += PAGE_SIZE) {

			if (offset + PAGE_SIZE > os->bootstrap_mem_end) {
				printk("builtin: OS is too big to load.\n");
				return -E2BIG;
			}

			buf = ihk_smp_map_virtual(offset, PAGE_SIZE);
			memset(buf, '\0', PAGE_SIZE);
			ihk_smp_unmap_virtual(buf);
			offset += PAGE_SIZE;
		}

		if (offset > maxoffset)
			maxoffset = offset;
	}

	fput(file);
	ihk_smp_unmap_virtual(elf64);

	pml4_p = os->bootstrap_mem_end - PAGE_SIZE;
	pdp_p = pml4_p - PAGE_SIZE;
	pde_p = pdp_p - PAGE_SIZE;

	cr3 = ident_page_table_virt;
	pml4 = ihk_smp_map_virtual(pml4_p, PAGE_SIZE);
	pdp = ihk_smp_map_virtual(pdp_p, PAGE_SIZE);
	pde = ihk_smp_map_virtual(pde_p, PAGE_SIZE);

	memset(pml4, '\0', PAGE_SIZE);
	memset(pdp, '\0', PAGE_SIZE);
	memset(pde, '\0', PAGE_SIZE);

	/*
	 * TODO: do this mapping so that holes between memory chunks
	 * are emitted
	 */
	pml4[0] = cr3[0];
	pml4[(MAP_ST_START >> PTL4_SHIFT) & 511] = cr3[0];
	pml4[(MAP_KERNEL_START >> PTL4_SHIFT) & 511] = pdp_p | 3;
	pdp[(MAP_KERNEL_START >> PTL3_SHIFT) & 511] = pde_p | 3;
	n = (os->bootstrap_mem_end - os->bootstrap_mem_start) >> PTL2_SHIFT;
	if(n > 511)
		n = 511;

	for (i = 0; i < n; i++) {
		pde[i] = (phys + (i << PTL2_SHIFT)) | 0x83;
	}
	startup_p = (os->bootstrap_mem_end & LARGE_PAGE_MASK) - (2 << PTL2_SHIFT);
	pde[511] = startup_p | 0x83;

	ihk_smp_unmap_virtual(pde);
	ihk_smp_unmap_virtual(pdp);
	ihk_smp_unmap_virtual(pml4);

	startup = ihk_smp_map_virtual(startup_p, PAGE_SIZE);
	memcpy(startup, startup_data, startup_data_end - startup_data);
	startup[2] = pml4_p;
	startup[3] = 0xffffffffc0000000;
	startup[4] = phys;
	startup[5] = trampoline_phys;
	startup[6] = entry;
	ihk_smp_unmap_virtual(startup);
	os->boot_rip = startup_p;

	set_os_status(os, BUILTIN_OS_STATUS_INITIAL);
	return 0;
}

static int smp_ihk_os_load_mem(ihk_os_t ihk_os, void *priv, const char *buf,
                               unsigned long size, long offset)
{
	struct smp_os_data *os = priv;
	unsigned long phys, to_read, flags;
	void *virt;

	dprint_func_enter;

	/* We just load from the lowest address of the private memory */
	if (!CORE_ISSET_ANY(&os->cpu_hw_ids_map) || os->mem_end - os->mem_start < 0) {
		printk("builtin: OS is not ready to boot.\n");
		return -EINVAL;
	}
	if (os->mem_start + size > os->mem_end) {
		printk("builtin: OS is too big to load.\n");
		return -E2BIG;
	}

	spin_lock_irqsave(&os->lock, flags);
	if (os->status != BUILTIN_OS_STATUS_INITIAL) {
		printk("builtin: OS status is not initial.\n");
		spin_unlock_irqrestore(&os->lock, flags);
		return -EBUSY;
	}
	os->status = BUILTIN_OS_STATUS_LOADING;
	spin_unlock_irqrestore(&os->lock, flags);

	offset += os->mem_start;
	phys = (offset & PAGE_MASK);
	offset -= phys;

	for (; size > 0; ) {
		virt = ihk_smp_map_virtual(phys, PAGE_SIZE);
		if (!virt) {
			dprintf("builtin: Failed to map %lx\n", phys);

			set_os_status(os, BUILTIN_OS_STATUS_INITIAL);

			return -ENOMEM;
		}
		if ((offset & (PAGE_SIZE - 1)) + size > PAGE_SIZE) {
			to_read = PAGE_SIZE - (offset & (PAGE_SIZE - 1));
		} else {
			to_read = size;
		}
		dprintf("memcpy(%p[%lx], buf + %lx, %lx)\n",
		        virt, phys, offset, to_read);
		memcpy(virt, buf + offset, to_read);

		/* Offset is only non-aligned at the first copy */
		offset += to_read;
		size -= to_read;
		ihk_smp_unmap_virtual(virt);

		phys += PAGE_SIZE;
	}

	os->boot_rip = os->mem_start;

	set_os_status(os, BUILTIN_OS_STATUS_INITIAL);
	
	return 0;
}

static void add_free_mem_chunk(struct chunk *chunk)
{
	struct chunk *chunk_iter;
	int added = 0;

	list_for_each_entry(chunk_iter, &ihk_mem_free_chunks, chain) {

		if (chunk_iter->addr > chunk->addr) {

			/* Add in front of this chunk */
			list_add_tail(&chunk->chain, &chunk_iter->chain);

			added = 1;
			break;
		}
	}

	/* All chunks start on lower memory or list was empty */
	if (!added) {
		list_add_tail(&chunk->chain, &ihk_mem_free_chunks);
	}

	dprintf("IHK-SMP: free mem chunk 0x%lx - 0x%lx added\n",
			chunk->addr, 
			chunk->addr + chunk->size);
}

static void merge_mem_chunks(struct list_head *chunks)
{
	struct chunk *mem_chunk;
	struct chunk *mem_chunk_next;

rerun:
	list_for_each_entry_safe(mem_chunk, mem_chunk_next, chunks, chain) {

		if (mem_chunk != mem_chunk_next &&
			mem_chunk_next->addr == mem_chunk->addr + mem_chunk->size &&
			mem_chunk_next->numa_id == mem_chunk->numa_id) {
			
			dprintf("IHK-SMP: free 0x%lx - 0x%lx and 0x%lx - 0x%lx merged\n",
				mem_chunk->addr,
				mem_chunk->addr + mem_chunk->size,
				mem_chunk_next->addr,
				mem_chunk_next->addr + mem_chunk_next->size);

			mem_chunk->size = mem_chunk->size + mem_chunk_next->size;
			list_del(&mem_chunk_next->chain);
				
			goto rerun;
		}
	}
}

/* TODO: rewrite this to embed in allocation and keep track
 * of max on the fly */
static size_t max_size_mem_chunk(struct rb_root *root)
{
	size_t max = 0;
	struct rb_node *node;

	for (node = rb_first(root); node; node = rb_next(node)) {
		struct chunk *chunk = container_of(node, struct chunk, node);
		if (chunk->size > max) {
			max = chunk->size;
		}
	}

	return max;
}

static int smp_ihk_os_shutdown(ihk_os_t ihk_os, void *priv, int flag)
{
	struct smp_os_data *os = priv;
	int i;
	struct ihk_os_mem_chunk *os_mem_chunk = NULL;
	struct ihk_os_mem_chunk *next_chunk = NULL;
	struct chunk *mem_chunk;
	
	if(os->status == BUILTIN_OS_STATUS_SHUTDOWN) {
		eprintk("%s,already down\n", __FUNCTION__);
		return 0;
	}

	/* Reset CPU cores used by this OS */
	for (i = 0; i < SMP_MAX_CPUS; ++i) {
		
		if (ihk_smp_cpus[i].os != ihk_os) 
			continue;

		ihk_smp_reset_cpu(ihk_smp_cpus[i].apic_id);
		ihk_smp_cpus[i].status = IHK_SMP_CPU_AVAILABLE;
		ihk_smp_cpus[i].os = (ihk_os_t)0;

		dprintk("IHK-SMP: CPU %d has been deassigned, APIC: %d\n", 
			ihk_smp_cpus[i].id, ihk_smp_cpus[i].apic_id);
	}
	os->nr_cpus = 0;

	/* Drop memory chunk used by this OS */
	list_for_each_entry_safe(os_mem_chunk, next_chunk,
			&ihk_mem_used_chunks, list) {

		if (os_mem_chunk->os != ihk_os) {
			continue;
		}

		list_del(&os_mem_chunk->list);

		mem_chunk = (struct chunk*)phys_to_virt(os_mem_chunk->addr);
		mem_chunk->addr = os_mem_chunk->addr;
		mem_chunk->size = os_mem_chunk->size;
		mem_chunk->numa_id = os_mem_chunk->numa_id;
		INIT_LIST_HEAD(&mem_chunk->chain);

		dprintk("IHK-SMP: mem chunk: 0x%lx - 0x%lx (len: %lu) freed\n",
				mem_chunk->addr, mem_chunk->addr + mem_chunk->size,
				mem_chunk->size);

		add_free_mem_chunk(mem_chunk);

		kfree(os_mem_chunk);
	}

	set_os_status(os, BUILTIN_OS_STATUS_SHUTDOWN);
	if (os->numa_mapping) {
		kfree(os->numa_mapping);
		os->numa_mapping = NULL;
	}

	if (os->param && os->param_pages_order) {
		free_pages((unsigned long)os->param, os->param_pages_order);
	}

	//kfree(os); /* done in destroy */

	return 0;
}


static int smp_ihk_os_alloc_resource(ihk_os_t ihk_os, void *priv,
                                     struct ihk_resource *resource)
{
	struct smp_os_data *os = priv;
	int i, ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&os->lock, flags);
	if (os->status != BUILTIN_OS_STATUS_INITIAL) {
		spin_unlock_irqrestore(&os->lock, flags);
		return -EBUSY;
	}
	os->status = BUILTIN_OS_STATUS_LOADING;
	spin_unlock_irqrestore(&os->lock, flags);

	/* Assign CPU cores */
	if (resource->cpu_cores) {
		int ihk_smp_nr_avail_cpus = 0;
		int ihk_smp_nr_allocated_cpus = 0;

		/* Check the number of available CPUs */
		for (i = 0; i < SMP_MAX_CPUS; i++) {
			if (ihk_smp_cpus[i].status == IHK_SMP_CPU_AVAILABLE) {
				++ihk_smp_nr_avail_cpus;
			}
		}

		if (resource->cpu_cores > ihk_smp_nr_avail_cpus) {
			printk("IHK-SMP: error: %d CPUs requested, but only %d available\n",
					resource->cpu_cores, ihk_smp_nr_avail_cpus);
			return -EINVAL;
		}

		/* Assign cores */
		for (i = 0; i < SMP_MAX_CPUS &&
			ihk_smp_nr_allocated_cpus < resource->cpu_cores; i++) {
			if (ihk_smp_cpus[i].status != IHK_SMP_CPU_AVAILABLE) {
				continue;
			}

			printk("IHK-SMP: CPU APIC %d assigned.\n",
					ihk_smp_cpus[i].apic_id);
			CORE_SET(ihk_smp_cpus[i].apic_id, os->cpu_hw_ids_map);

			ihk_smp_cpus[i].status = IHK_SMP_CPU_ASSIGNED;
			ihk_smp_cpus[i].os = ihk_os;

			++ihk_smp_nr_allocated_cpus;
		}
	}

	/* Assign memory */
	if (resource->mem_size) {
		struct ihk_os_mem_chunk *os_mem_chunk;
		struct chunk *mem_chunk_leftover;
		struct chunk *mem_chunk_iter;
		os_mem_chunk = kmalloc(sizeof(struct ihk_os_mem_chunk), GFP_KERNEL);

		if (!os_mem_chunk) {
			printk("IHK-DMP: error: allocating os_mem_chunk\n");
			return -ENOMEM;
		}

		os_mem_chunk->addr = 0;
		INIT_LIST_HEAD(&os_mem_chunk->list);

		list_for_each_entry(mem_chunk_iter, &ihk_mem_free_chunks, chain) {
			if (mem_chunk_iter->size >= resource->mem_size) {

				os_mem_chunk->addr = mem_chunk_iter->addr;
				os_mem_chunk->size = resource->mem_size;
				os_mem_chunk->os = ihk_os;
				os_mem_chunk->numa_id = mem_chunk_iter->numa_id;

				list_del(&mem_chunk_iter->chain);
				break;
			}
		}

		if (!os_mem_chunk->addr) {
			printk("IHK-SMP: error: not enough memory\n");
			ret = -ENOMEM;
			goto error_drop_cores;
		}

		list_add(&os_mem_chunk->list, &ihk_mem_used_chunks);
		resource->mem_start = os_mem_chunk->addr;

		/* Split if there is any leftover */
		if (mem_chunk_iter->size > resource->mem_size) {
			mem_chunk_leftover = (struct chunk*)
				phys_to_virt(mem_chunk_iter->addr + resource->mem_size);
			mem_chunk_leftover->addr = mem_chunk_iter->addr +
				resource->mem_size;
			mem_chunk_leftover->size = mem_chunk_iter->size -
				resource->mem_size;
			mem_chunk_leftover->numa_id = mem_chunk_iter->numa_id;

			add_free_mem_chunk(mem_chunk_leftover);
		}

		os->mem_start = resource->mem_start;
		os->mem_end = os->mem_start + resource->mem_size;

		dprintf("IHK-SMP: memory 0x%lx - 0x%lx allocated.\n",
				os->mem_start, os->mem_end);
	}

	set_os_status(os, BUILTIN_OS_STATUS_INITIAL);
	return 0;

error_drop_cores:
	/* Drop CPU cores for this OS */
	for (i = 0; i < SMP_MAX_CPUS; ++i) {

		if (ihk_smp_cpus[i].status != IHK_SMP_CPU_ASSIGNED ||
			ihk_smp_cpus[i].os != ihk_os)
			continue;

		printk("IHK-SMP: CPU APIC %d deassigned.\n",
				ihk_smp_cpus[i].apic_id);

		ihk_smp_cpus[i].status = IHK_SMP_CPU_AVAILABLE;
		ihk_smp_cpus[i].os = (ihk_os_t)0;
	}

	return ret;
}

static enum ihk_os_status smp_ihk_os_query_status(ihk_os_t ihk_os, void *priv)
{
	struct smp_os_data *os = priv;
	int status;

	status = os->status;

	if (status == BUILTIN_OS_STATUS_BOOTING) {
		if (os->param->status == 1) {
			return IHK_OS_STATUS_BOOTED;
		} else if(os->param->status == 2) {
			/* Restore Linux trampoline once ready */
			if (using_linux_trampoline) {
				memcpy(trampoline_va, linux_trampoline_backup, 
						IHK_SMP_TRAMPOLINE_SIZE);
			}
			return IHK_OS_STATUS_READY;
		} else {
			return IHK_OS_STATUS_BOOTING;
		}
	} else {
		return IHK_OS_STATUS_NOT_BOOTED;
	}
}

static int smp_ihk_os_set_kargs(ihk_os_t ihk_os, void *priv, char *buf)
{
	unsigned long flags;
	struct smp_os_data *os = priv;

	spin_lock_irqsave(&os->lock, flags);
	if (os->status != BUILTIN_OS_STATUS_INITIAL) {
		printk("builtin: OS status is not initial.\n");
		spin_unlock_irqrestore(&os->lock, flags);
		return -EBUSY;
	}
	os->status = BUILTIN_OS_STATUS_LOADING;
	spin_unlock_irqrestore(&os->lock, flags);

	strncpy(os->kernel_args, buf, sizeof(os->kernel_args));
	dprintk("%s,kernel_args=%s\n", __FUNCTION__, os->kernel_args);

	set_os_status(os, BUILTIN_OS_STATUS_INITIAL);

	return 0;
}

static int smp_ihk_os_send_nmi(ihk_os_t ihk_os, void *priv, int mode)
{
	struct smp_os_data *os = priv;
	int i;
	unsigned long rpa;
	unsigned long size;
	int *nmi_mode;
	unsigned long pa;
	unsigned long psize;

	if (smp_ihk_os_get_special_addr(ihk_os, priv, IHK_SPADDR_NMI_MODE,
				        &rpa, &size)) {
		return -EINVAL;
	}

	psize = PAGE_SIZE;
	pa = smp_ihk_os_map_memory(ihk_os, priv, rpa, psize);
	nmi_mode = smp_ihk_map_virtual(ihk_os, priv, pa, psize,
					  NULL, 0);
	*nmi_mode = mode;
	smp_ihk_unmap_virtual(ihk_os, priv, nmi_mode, psize);
	smp_ihk_unmap_memory(ihk_os, priv, pa, psize);

	for (i = 0; i < os->cpu_info.n_cpus; i++) {

#ifdef CONFIG_X86_X2APIC
		if (x2apic_is_enabled()) {
			safe_apic_wait_icr_idle();
			apic_icr_write(APIC_DM_NMI, os->cpu_info.hw_ids[i]);
		}
		else 		
#endif
		{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,6,0)
			___default_send_IPI_dest_field(
					os->cpu_info.hw_ids[i],
					NMI_VECTOR, APIC_DEST_PHYSICAL);
#else
			__default_send_IPI_dest_field(
					os->cpu_info.hw_ids[i],
					NMI_VECTOR, APIC_DEST_PHYSICAL);
#endif
		}
	}
	return 0;
}

static int smp_ihk_os_dump(ihk_os_t ihk_os, void *priv, dumpargs_t *args)
{
	struct smp_os_data *os = priv;

	if (0) printk("mcosdump: cmd %d start %lx size %lx buf %p\n",
			args->cmd, args->start, args->size, args->buf);

	if (args->cmd == DUMP_NMI) {
		smp_ihk_os_send_nmi(ihk_os, priv, 0);
		return 0;
	}

	if (args->cmd == DUMP_QUERY) {
		int i = 0;
		struct ihk_os_mem_chunk *os_mem_chunk;
		dump_mem_chunks_t *mem_chunks = args->buf;

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
			(os->bootstrap_mem_start + LARGE_PAGE_SIZE * 2 - 1) & LARGE_PAGE_MASK;

		return 0;
	}

	if (args->cmd == DUMP_READ) {
		void *va;

		va = phys_to_virt(args->start);
		if (copy_to_user(args->buf, va, args->size)) {
			return -EFAULT;
		}
		return 0;
	}

	if (args->cmd == DUMP_QUERY_ALL) {
		args->start = os->mem_start;
		args->size = os->mem_end - os->mem_start;
		return 0;
	}

	if (args->cmd == DUMP_READ_ALL) {
		void *va;

		va = phys_to_virt(args->start);
		if (copy_to_user(args->buf, va, args->size)) {
			return -EFAULT;
		}
		return 0;
	}

	return -EINVAL;
}

static int smp_ihk_os_wait_for_status(ihk_os_t ihk_os, void *priv,
                                      enum ihk_os_status status, 
                                      int sleepable, int timeout)
{
	enum ihk_os_status s;
	if (sleepable) {
		/* TODO: Enable notification of status change, and wait */
		return -1;
	} else {
		/* Polling */
		while ((s = smp_ihk_os_query_status(ihk_os, priv)),
		       s != status && s < IHK_OS_STATUS_SHUTDOWN 
		       && timeout > 0) {
			mdelay(100);
			timeout--;
		}
		return s == status ? 0 : -1;
	}
}

static int smp_ihk_os_issue_interrupt(ihk_os_t ihk_os, void *priv,
                                      int cpu, int v)
{
	struct smp_os_data *os = priv;
	unsigned long flags;

	/* better calcuation or make map */
	if (cpu < 0 || cpu >= os->cpu_info.n_cpus) {
		return -EINVAL;
	}
	//printk("smp_ihk_os_issue_interrupt(): %d\n", os->cpu_info.hw_ids[cpu]);
	//shimos_issue_ipi(os->cpu_info.hw_ids[cpu], v);
	
	local_irq_save(flags);
#ifdef CONFIG_X86_X2APIC
	if (x2apic_is_enabled()) {
		native_x2apic_icr_write(v, os->cpu_info.hw_ids[cpu]);
	}
	else
#endif
	{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,6,0)
		___default_send_IPI_dest_field(os->cpu_info.hw_ids[cpu], v, 
			APIC_DEST_PHYSICAL);
#else
		__default_send_IPI_dest_field(os->cpu_info.hw_ids[cpu], v, 
			APIC_DEST_PHYSICAL);
#endif
	}
	local_irq_restore(flags);

	return -EINVAL;
}

static unsigned long smp_ihk_os_map_memory(ihk_os_t ihk_os, void *priv,
                                           unsigned long remote_phys,
                                           unsigned long size)
{
	/* We use the same physical memory. So no need to do something */
	return remote_phys;
}

static int smp_ihk_os_unmap_memory(ihk_os_t ihk_os, void *priv,
                                    unsigned long local_phys,
                                    unsigned long size)
{
	return 0;
}

static int smp_ihk_os_get_special_addr(ihk_os_t ihk_os, void *priv,
                                       enum ihk_special_addr_type type,
                                       unsigned long *addr,
                                       unsigned long *size)
{
	struct smp_os_data *os = priv;

	if (!os->param) {
		return -EINVAL;
	}

	switch (type) {
	case IHK_SPADDR_KMSG:
		if (os->param->msg_buffer) {
			*addr = os->param->msg_buffer;
			*size = os->param->msg_buffer_size;
			return 0;
		}
		break;

	case IHK_SPADDR_MIKC_QUEUE_RECV:
		if (os->param->mikc_queue_recv) {
			*addr = os->param->mikc_queue_recv;
			*size = MASTER_IKCQ_SIZE;
			return 0;
		}
		break;
	case IHK_SPADDR_MIKC_QUEUE_SEND:
		if (os->param->mikc_queue_send) {
			*addr = os->param->mikc_queue_send;
			*size = MASTER_IKCQ_SIZE;
			return 0;
		}
		break;
	case IHK_SPADDR_MONITOR:
		if (os->param->monitor) {
			*addr = os->param->monitor;
			*size = os->param->monitor_size;
			return 0;
		}
		break;
	case IHK_SPADDR_NMI_MODE:
		if (os->param->nmi_mode_addr) {
			*addr = os->param->nmi_mode_addr;
			*size = sizeof(int);
			return 0;
		}
		break;
	}

	return -EINVAL;
}

static long smp_ihk_os_debug_request(ihk_os_t ihk_os, void *priv,
                                     unsigned int req, unsigned long arg)
{
	switch (req) {
	case IHK_OS_DEBUG_START:
		smp_ihk_os_issue_interrupt(ihk_os, priv, (arg >> 8),
		                           (arg & 0xff));
		return 0;
	}
	return -EINVAL;
}

static LIST_HEAD(builtin_interrupt_handlers);

static int smp_ihk_os_register_handler(ihk_os_t os, void *os_priv, int itype,
                                       struct ihk_host_interrupt_handler *h)
{
	h->os = os;
	h->os_priv = os_priv;
	list_add_tail(&h->list, &builtin_interrupt_handlers);

	return 0;
}

static int smp_ihk_os_unregister_handler(ihk_os_t os, void *os_priv, int itype,
                                         struct ihk_host_interrupt_handler *h)
{
	list_del(&h->list);
	return 0;
}

static irqreturn_t smp_ihk_irq_call_handlers(int irq, void *data)
{
	struct ihk_host_interrupt_handler *h;

	/* XXX: Linear search? */
	list_for_each_entry(h, &builtin_interrupt_handlers, list) {
		if (h->func) {
			h->func(h->os, h->os_priv, h->priv);
		}
	}

	return IRQ_HANDLED;
}

static struct ihk_mem_info *smp_ihk_os_get_memory_info(ihk_os_t ihk_os,
                                                       void *priv)
{
	struct smp_os_data *os = priv;

	return &os->mem_info;
}

static struct ihk_cpu_info *smp_ihk_os_get_cpu_info(ihk_os_t ihk_os, void *priv)
{
	struct smp_os_data *os = priv;

	return &os->cpu_info;
}

/*
 * Parse the CPU list string and assign CPUs in the order of designation.
 * NOTE: The string must be valid.
 */
static int __assign_cpus(ihk_os_t ihk_os, struct smp_os_data *os, char *buf)
{
	unsigned a, b;
	int c, old_c, totaldigits, ndigits;
	int at_start, in_range;

	totaldigits = c = 0;
	do {
		at_start = 1;
		in_range = 0;
		a = b = 0;
		ndigits = totaldigits;

		/* Get the next cpu# or a range of cpu#'s */
		for (;;) {
			old_c = c;
			c = *buf++;

			/* End of string? */
			if (!c)
				break;

			if (isspace(c))
				continue;

			/* A '\0' or a ',' signal the end of a cpu# or range */
			if (c == '\0' || c == ',')
				break;
			/*
			* whitespaces between digits are not allowed,
			* but it's ok if whitespaces are on head or tail.
			* when old_c is whilespace,
			* if totaldigits == ndigits, whitespace is on head.
			* if whitespace is on tail, it should not run here.
			* as c was ',' or '\0',
			* the last code line has broken the current loop.
			*/
			if ((totaldigits != ndigits) && isspace(old_c))
				return -EINVAL;

			if (c == '-') {
				if (at_start || in_range)
					return -EINVAL;
				b = 0;
				in_range = 1;
				at_start = 1;
				continue;
			}

			if (!isdigit(c))
				return -EINVAL;

			b = b * 10 + (c - '0');
			if (!in_range)
				a = b;
			at_start = 0;
			totaldigits++;
		}
		if (ndigits == totaldigits)
			continue;
		/* if no digit is after '-', it's wrong*/
		if (at_start && in_range)
			return -EINVAL;
		if (!(a <= b))
			return -EINVAL;

		/* Assign CPUs and update CPU mapping */
		while (a <= b) {
			int cpu = a;
			dprintk(KERN_INFO "IHK-SMP: assigned CPU %d to OS %p\n", a, ihk_os);

			CORE_SET(ihk_smp_cpus[cpu].apic_id, os->cpu_hw_ids_map);
			set_bit(cpu_to_node(cpu), &os->numa_mask);

			ihk_smp_cpus[cpu].status = IHK_SMP_CPU_ASSIGNED;
			ihk_smp_cpus[cpu].os = ihk_os;
			ihk_smp_cpus[cpu].ikc_map_cpu = 0;

			os->cpu_mapping[os->nr_cpus] = cpu;
			os->cpu_hw_ids[os->nr_cpus] = ihk_smp_cpus[cpu].apic_id;
			os->nr_cpus++;

			a++;
		}
	}
	while (c);

	return 0;
}

static int smp_ihk_os_assign_cpu(ihk_os_t ihk_os, void *priv, unsigned long arg)
{
	int ret;
	int cpu;
	struct smp_os_data *os = priv;
	cpumask_t cpus_to_assign;
	unsigned long flags;
	ihk_resource_req_t req;
	char *req_string = NULL;

	spin_lock_irqsave(&os->lock, flags);
	if (os->status != BUILTIN_OS_STATUS_INITIAL) {
		spin_unlock_irqrestore(&os->lock, flags);
		return -EBUSY;
	}
	spin_unlock_irqrestore(&os->lock, flags);

	if (copy_from_user(&req, (void *)arg, sizeof(req))) {
		printk("%s: error: copying request\n", __FUNCTION__);
		return -EFAULT;
	}

	if (req.string_len == 0) {
		printk("%s: invalid request length\n", __FUNCTION__);
		return -EINVAL;
	}

	req_string = kmalloc(req.string_len + 1, GFP_KERNEL);
	if (!req_string) {
		printk("%s: error: allocating request string\n", __FUNCTION__);
		return -EINVAL;
	}

	if (copy_from_user(req_string, req.string, req.string_len + 1)) {
		printk("%s: error: copying request string\n", __FUNCTION__);
		ret = -EFAULT;
		goto out;
	}

	memset(&cpus_to_assign, 0, sizeof(cpus_to_assign));

	/* Validate CPU list provided by user */
	if (cpulist_parse(req_string, &cpus_to_assign) < 0) {
		printk("%s: invalid CPUs requested\n", __FUNCTION__);
		ret = -EINVAL;
		goto out;
	}

	/* Check if cores to be assigned are available */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,0,0)
	for_each_cpu(cpu, &cpus_to_assign) {
#else
	for_each_cpu_mask(cpu, cpus_to_assign) {
#endif
		if (ihk_smp_cpus[cpu].status != IHK_SMP_CPU_AVAILABLE) {
			printk("IHK-SMP: error: CPU core %d is not available for assignment\n", cpu);
			ret = -EINVAL;
			goto out;
		}
	}

	ret = __assign_cpus(ihk_os, os, req_string);
	if (ret) {
		printk("%s: error: assigning CPUs: %s\n", __FUNCTION__, req_string);
		goto out;
	}

	printk(KERN_INFO "IHK-SMP: CPUs: %s assigned to OS %p\n", req_string, ihk_os);

out:
	if (req_string) kfree(req_string);
	return ret;
}

static int smp_ihk_os_release_cpu(ihk_os_t ihk_os, void *priv, unsigned long arg)
{
	int ret;
	int cpu;
	struct smp_os_data *os = priv;
	cpumask_t cpus_to_release;
	unsigned long flags;
	ihk_resource_req_t req;
	char *req_string = NULL;

	spin_lock_irqsave(&os->lock, flags);
	if (os->status != BUILTIN_OS_STATUS_INITIAL) {
		spin_unlock_irqrestore(&os->lock, flags);
		return -EBUSY;
	}
	spin_unlock_irqrestore(&os->lock, flags);

	if (copy_from_user(&req, (void *)arg, sizeof(req))) {
		printk("%s: error: copying request\n", __FUNCTION__);
		return -EFAULT;
	}

	if (req.string_len == 0) {
		printk("%s: invalid request length\n", __FUNCTION__);
		return -EINVAL;
	}

	req_string = kmalloc(req.string_len + 1, GFP_KERNEL);
	if (!req_string) {
		printk("%s: error: allocating request string\n", __FUNCTION__);
		return -EINVAL;
	}

	if (copy_from_user(req_string, req.string, req.string_len + 1)) {
		printk("%s: error: copying request string\n", __FUNCTION__);
		ret = -EFAULT;
		goto out;
	}

	memset(&cpus_to_release, 0, sizeof(cpus_to_release));

	/* Parse CPU list provided by user */
	if (cpulist_parse(req_string, &cpus_to_release) < 0) {
		printk("%s: invalid CPUs requested\n", __FUNCTION__);
		ret = -EINVAL;
		goto out;
	}

	/* Check if cores to be released are assigned to this OS */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,0,0)
	for_each_cpu(cpu, &cpus_to_release) {
#else
	for_each_cpu_mask(cpu, cpus_to_release) {
#endif
		if (ihk_smp_cpus[cpu].status != IHK_SMP_CPU_ASSIGNED ||
			ihk_smp_cpus[cpu].os != ihk_os) {
			printk("IHK-SMP: error: CPU core %d is not assigned to %p\n", 
				cpu, ihk_os);
			ret = -EINVAL;
			goto out;
		}
	}

	/* Do the release */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,0,0)
	for_each_cpu(cpu, &cpus_to_release) {
#else
	for_each_cpu_mask(cpu, cpus_to_release) {
#endif
		int lwk_cpu;

		ihk_smp_reset_cpu(ihk_smp_cpus[cpu].apic_id);
		CORE_CLR(ihk_smp_cpus[cpu].apic_id, os->cpu_hw_ids_map);

		ihk_smp_cpus[cpu].status = IHK_SMP_CPU_AVAILABLE;
		ihk_smp_cpus[cpu].os = (ihk_os_t)0;

		/* Update CPU mapping */
		for (lwk_cpu = 0; lwk_cpu < os->nr_cpus; ++lwk_cpu) {
			int _lwk_cpu;

			if (os->cpu_mapping[lwk_cpu] != cpu)
				continue;

			/* Shift down the rest of the array */
			for (_lwk_cpu = lwk_cpu + 1;
				_lwk_cpu < os->nr_cpus; ++_lwk_cpu) {
				os->cpu_mapping[_lwk_cpu - 1] = os->cpu_mapping[_lwk_cpu];
				os->cpu_hw_ids[_lwk_cpu - 1] = os->cpu_hw_ids[_lwk_cpu];
			}

			--os->nr_cpus;
			break;
		}

		dprintk(KERN_INFO "IHK-SMP: CPU APIC %d released from %p\n",
				ihk_smp_cpus[cpu].apic_id, ihk_os);
	}

	printk(KERN_INFO "IHK-SMP: released CPUs: %s from OS %p\n",
		req_string, ihk_os);

	ret = 0;

out:
	if (req_string) kfree(req_string);
	return ret;
}

char query_res[8192];

static int smp_ihk_os_query_cpu(ihk_os_t ihk_os, void *priv, unsigned long arg)
{
	int i, ret = 0;
	struct smp_os_data *os = priv;
	char cpu_str[64];

	memset(query_res, 0, sizeof(query_res));

	/* Respect the order of cpus specified when assigining them
	   e.g. 0,2,1,3 */
	for(i = 0; i < os->nr_cpus; ++i) {
		sprintf(cpu_str, "%d", os->cpu_mapping[i]);
		strcat(query_res, cpu_str);
		if(i != os->nr_cpus - 1) {
			strcat(query_res, ",");
		}
	}

	dprintk("%s,query_res=%s\n", __FUNCTION__, query_res);

	if (strlen(query_res) > 0) {
		if (copy_to_user((char *)arg, query_res, strlen(query_res) + 1)) {
			printk("IHK-SMP: error: copying CPU string to user-space\n");
			ret = -EINVAL;
			goto fn_fail;
		}
	}


 fn_exit:
	return 0;
 fn_fail:
	goto fn_exit;
}

static int smp_ihk_os_ikc_map(ihk_os_t ihk_os, void *priv, unsigned long arg)
{
	int ret = 0;
	struct smp_os_data *os = priv;
	cpumask_t cpus_to_map;
	unsigned long flags;
	char *string = (char *)arg;
	char *token;

	dprintk("%s,ikc_map,arg=%s\n", __FUNCTION__, string);

	spin_lock_irqsave(&os->lock, flags);
	if (os->status != BUILTIN_OS_STATUS_INITIAL) {
		spin_unlock_irqrestore(&os->lock, flags);
		ret = -EBUSY;
		goto out;
	}
	spin_unlock_irqrestore(&os->lock, flags);

	token = strsep(&string, "+");
	while (token) {
		char *cpu_list;
		char *ikc_cpu;
		int cpu;

		cpu_list = strsep(&token, ":");
		if (!cpu_list) {
			ret = -EINVAL;
			goto out;
		}

		memset(&cpus_to_map, 0, sizeof(cpus_to_map));
		cpulist_parse(cpu_list, &cpus_to_map);

		ikc_cpu = strsep(&token, ":");
		if (!ikc_cpu) {
			ret = -EINVAL;
			goto out;
		}

		printk("%s: %s -> %s\n", __FUNCTION__, cpu_list, ikc_cpu);
		/* Store IKC target CPU */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,0,0)
		for_each_cpu(cpu, &cpus_to_map) {
#else
		for_each_cpu_mask(cpu, cpus_to_map) {
#endif
			/* TODO: check if CPU belongs to OS */
			if (kstrtoint(ikc_cpu, 10, &ihk_smp_cpus[cpu].ikc_map_cpu)) {
				ret = -EINVAL;
				goto out;
			}
		}

		token = strsep(&string, "+");
	}
	/* Mapping has been requested */
	os->cpu_ikc_mapped = 1;

out:
	return ret;
}

static int smp_ihk_os_query_ikc_map(ihk_os_t ihk_os, void *priv, unsigned long arg)
{
	int ret = 0;
	int i, src, dst, max_dst = -1;
	char cpu_str[4];

	/* Sender-set (sset): Set of senders sharing the same destination */
	int *rank = NULL; /* Order in sender-set, indexed by IKC source CPU# */
	int *ikc_sset_sizes = NULL; /* Indexed by IKC destination CPU# */
	int **ikc_sset_members = NULL; /* Indexed by IKC destination CPU# */

	rank = kzalloc(sizeof(int) * SMP_MAX_CPUS, GFP_KERNEL);
	if (!rank) {
		printk(KERN_ERR "IHK-SMP: error: allocating rank\n");
		ret = -ENOMEM;
		goto fn_fail;
	}

	ikc_sset_sizes = kzalloc(sizeof(int) * SMP_MAX_CPUS, GFP_KERNEL);
	if (!ikc_sset_sizes) {
		printk(KERN_ERR "IHK-SMP: error: allocating num_ikc_ssets\n");
		ret = -ENOMEM;
		goto fn_fail;
	}

	ikc_sset_members = kzalloc(sizeof(int*) * SMP_MAX_CPUS, GFP_KERNEL);
	if (!ikc_sset_members) {
		printk(KERN_ERR "IHK-SMP: error: allocating ikc_sset_members\n");
		ret = -ENOMEM;
		goto fn_fail;
	}

	for (src = 0; src < SMP_MAX_CPUS; ++src) {
		if (ihk_smp_cpus[src].status != IHK_SMP_CPU_ASSIGNED)
			continue;
		if (ihk_smp_cpus[src].os != ihk_os)
			continue;

		rank[src] = ikc_sset_sizes[ihk_smp_cpus[src].ikc_map_cpu];
		//dprintk("query_ikc_map,src=%d,dst=%d,rank[src]=%d\n", src, ihk_smp_cpus[src].ikc_map_cpu, rank[src]);
		ikc_sset_sizes[ihk_smp_cpus[src].ikc_map_cpu]++;
		//dprintk("query_ikc_map,sset_sizes=%d\n", ikc_sset_sizes[ihk_smp_cpus[src].ikc_map_cpu]);
		if(max_dst < ihk_smp_cpus[src].ikc_map_cpu) {
			max_dst = ihk_smp_cpus[src].ikc_map_cpu;
		}
		//dprintk("query_ikc_map,max_dst=%d\n", max_dst);
	}

	for (src = 0; src < SMP_MAX_CPUS; ++src) {
		if (ihk_smp_cpus[src].status != IHK_SMP_CPU_ASSIGNED)
			continue;
		if (ihk_smp_cpus[src].os != ihk_os)
			continue;

		if (!ikc_sset_members[ihk_smp_cpus[src].ikc_map_cpu]) {
			ikc_sset_members[ihk_smp_cpus[src].ikc_map_cpu] = 
				kmalloc(sizeof(int) * ikc_sset_sizes[ihk_smp_cpus[src].ikc_map_cpu], GFP_KERNEL);
			if (!ikc_sset_members[ihk_smp_cpus[src].ikc_map_cpu]) {
				printk(KERN_ERR "IHK-SMP: error: allocating ikc_sset_members\n");
				ret = -ENOMEM;
				goto fn_fail;
			}
			//dprintk("query_ikc_map,kmalloc,dst=%d,sset_sizes=%d\n", ihk_smp_cpus[src].ikc_map_cpu, ikc_sset_sizes[ihk_smp_cpus[src].ikc_map_cpu]);
		}
		*(ikc_sset_members[ihk_smp_cpus[src].ikc_map_cpu] + rank[src]) = src;
		//dprintk("query_ikc_map,src=%d,dst=%d,*(members[dst]+rank[src])=%d\n", src, ihk_smp_cpus[src].ikc_map_cpu, *(ikc_sset_members[ihk_smp_cpus[src].ikc_map_cpu] + rank[src]));
	}

	memset(query_res, 0, sizeof(query_res));

	for (dst = 0; dst < SMP_MAX_CPUS; ++dst) {
		if(ikc_sset_sizes[dst] == 0) {
			continue;
		}

		for (i = 0; i < ikc_sset_sizes[dst]; ++i) {
			sprintf(cpu_str, "%d", *(ikc_sset_members[dst] + i));
			strcat(query_res, cpu_str);
			if(i != ikc_sset_sizes[dst] - 1) {
				strcat(query_res, ",");
			}
		}
		strcat(query_res, ":");
		sprintf(cpu_str, "%d", dst);
		strcat(query_res, cpu_str);
		if(dst != max_dst) {
			strcat(query_res, "+");
		}
	}

	dprintk("query_ikc_map,query_res=%s\n", query_res);

	if (strlen(query_res) >= 0) {
		if (copy_to_user((char *)arg, query_res, strlen(query_res) + 1)) {
			printk("IHK-SMP: error: copying CPU string to user-space\n");
			ret = -EINVAL;
			goto fn_fail;
		}
	}

 fn_fail:
	if(ikc_sset_members) {
		for (dst = 0; dst < SMP_MAX_CPUS; ++dst) {
			if(ikc_sset_members[dst]) {
				kfree(ikc_sset_members[dst]);
			}
		}
	}
	if(ikc_sset_members) {
		kfree(ikc_sset_members);
	}
	if(ikc_sset_sizes) {
		kfree(ikc_sset_sizes);
	}
	if(rank) {
		kfree(rank);
	}
	// fn_exit:
	return ret;
}

static int smp_ihk_parse_mem(char *p, size_t *mem_size, int *numa_id)
{
	char *oldp;

	/* Parse memory string provided by the user
	 * FIXME: validate userspace buffer */
	oldp = p;
	*mem_size = memparse(p, &p);
	if (p == oldp)
		return -EINVAL;

	if (!(*p)) {
		*numa_id = 0;
	}
	else {
		if (*p != '@') {
			return -EINVAL;
		}

		*numa_id = memparse(p + 1, &p);
	}

	dprintf("smp_ihk_parse_mem(): %lu @ %d parsed\n", *mem_size, *numa_id);

	return 0;
}

static int __smp_ihk_os_assign_mem(ihk_os_t ihk_os, struct smp_os_data *os,
		size_t mem_size, int numa_id)
{
	int ret = 0;
	struct ihk_os_mem_chunk *os_mem_chunk;
	struct ihk_os_mem_chunk *os_mem_chunk_tba_iter;
	struct ihk_os_mem_chunk *os_mem_chunk_tba_next = NULL;
	struct ihk_os_mem_chunk *os_mem_chunk_iter;
	struct ihk_os_mem_chunk *os_mem_chunk_next = NULL;
	struct chunk *mem_chunk_leftover;
	struct chunk *mem_chunk_iter;
	struct chunk *mem_chunk_max;
	struct chunk *mem_chunk_match;
	size_t mem_size_left = mem_size;
	struct list_head to_be_assigned_chunks;

	INIT_LIST_HEAD(&to_be_assigned_chunks);

	while (mem_size_left) {
		mem_size = mem_size_left;

		/* Allocate OS memory chunk descriptor */
		os_mem_chunk = kmalloc(sizeof(struct ihk_os_mem_chunk), GFP_KERNEL);

		if (!os_mem_chunk) {
			printk(KERN_ERR "IHK-SMP: error: allocating os_mem_chunk\n");
			ret = -ENOMEM;
			goto out;
		}

		os_mem_chunk->addr = 0;
		os_mem_chunk->numa_id = numa_id;
		INIT_LIST_HEAD(&os_mem_chunk->list);

		/* Find the biggest chunk or an exact match on this NUMA node */
		mem_chunk_max = NULL;
		mem_chunk_match = NULL;
		list_for_each_entry(mem_chunk_iter, &ihk_mem_free_chunks, chain) {
			if (mem_chunk_iter->numa_id != numa_id) {
				continue;
			}

			if (!mem_chunk_match && (mem_chunk_iter->size == mem_size)) {
				mem_chunk_match = mem_chunk_iter;
				break;
			}

			if (!mem_chunk_max || (mem_chunk_max->size < mem_chunk_iter->size)) {
				mem_chunk_max = mem_chunk_iter;
			}
		}

		if (!mem_chunk_max && !mem_chunk_match) {
			printk(KERN_ERR "IHK-SMP: error: not enough memory on ihk_mem_free_chunks\n");
			kfree(os_mem_chunk);
			ret = -ENOMEM;
			goto out;
		}

		os_mem_chunk->os = ihk_os;
		os_mem_chunk->numa_id = numa_id;

		/* Exact match? */
		if (mem_chunk_match) {
			os_mem_chunk->addr = mem_chunk_match->addr;
			os_mem_chunk->size = mem_chunk_match->size;

			list_del(&mem_chunk_match->chain);
		}
		else {
			os_mem_chunk->addr = mem_chunk_max->addr;
			os_mem_chunk->size = mem_size < mem_chunk_max->size ?
				mem_size : mem_chunk_max->size;

			list_del(&mem_chunk_max->chain);

			/* Split if there is any leftover */
			if (mem_chunk_max->size > mem_size) {
				struct page *pg;

				pg = virt_to_page(phys_to_virt(mem_chunk_max->addr + mem_size));
				/* Do not split compound pages though */
				if (PageTail(pg)) {
					size_t comp_size = PAGE_SIZE << compound_order(pg->first_page);

					if ((page_to_phys(pg->first_page) + comp_size) <
							mem_chunk_max->addr + mem_chunk_max->size) {
						off_t comp_end_offset = comp_size -
							(page_to_phys(pg) - page_to_phys(pg->first_page));

						mem_chunk_leftover = (struct chunk*)
							phys_to_virt(mem_chunk_max->addr + mem_size +
									comp_end_offset);
						mem_chunk_leftover->addr = mem_chunk_max->addr + mem_size +
							comp_end_offset;
						mem_chunk_leftover->size = mem_chunk_max->size - mem_size -
							comp_end_offset;
						mem_chunk_leftover->numa_id = mem_chunk_max->numa_id;
						add_free_mem_chunk(mem_chunk_leftover);
						dprintk("%s: comp_end_offset: %lu\n",
								__FUNCTION__, comp_end_offset);
					}
				}
				else {
					mem_chunk_leftover = (struct chunk*)
						phys_to_virt(mem_chunk_max->addr + mem_size);
					mem_chunk_leftover->addr = mem_chunk_max->addr + mem_size;
					mem_chunk_leftover->size = mem_chunk_max->size - mem_size;
					mem_chunk_leftover->numa_id = mem_chunk_max->numa_id;
					add_free_mem_chunk(mem_chunk_leftover);
				}
			}
		}

		list_add_tail(&os_mem_chunk->list, &to_be_assigned_chunks);
		mem_size_left -= os_mem_chunk->size;
	}

	/* We got all pieces we need, add them to the OS instance */
	list_for_each_entry_safe(os_mem_chunk_tba_iter, os_mem_chunk_tba_next,
			&to_be_assigned_chunks, list) {

		list_del(&os_mem_chunk_tba_iter->list);
		os_mem_chunk = os_mem_chunk_tba_iter;


		/* Insert the chunk in physical address ascending order */
		os_mem_chunk_next = NULL;
		list_for_each_entry(os_mem_chunk_iter, &ihk_mem_used_chunks, list) {
			if (os_mem_chunk->addr < os_mem_chunk_iter->addr) {
				os_mem_chunk_next = os_mem_chunk_iter;
				break;
			}
		}

		/* Add in front of next */
		if (os_mem_chunk_next) {
			list_add_tail(&os_mem_chunk->list, &os_mem_chunk_next->list);
			dprintf("IHK-SMP: memory 0x%lx - 0x%lx (len: %lu) @ NUMA node %d assigned to %p [in front of 0x%lx]\n",
					os_mem_chunk->addr, os_mem_chunk->addr + os_mem_chunk->size,
					os_mem_chunk->size, numa_id, ihk_os, os_mem_chunk_next->addr);
		}
		/* Add to the end */
		else {
			list_add_tail(&os_mem_chunk->list, &ihk_mem_used_chunks);
			dprintf("IHK-SMP: memory 0x%lx - 0x%lx (len: %lu) @ NUMA node %d assigned to %p [tail]\n",
					os_mem_chunk->addr, os_mem_chunk->addr + os_mem_chunk->size,
					os_mem_chunk->size, numa_id, ihk_os);
		}

		/* Update OS start and end addresses */
		if (!os->mem_start || os->mem_start > os_mem_chunk->addr) {
			os->mem_start = os_mem_chunk->addr;
		}
		if (!os->mem_end || os->mem_end < os_mem_chunk->addr + os_mem_chunk->size) {
			os->mem_end = os_mem_chunk->addr + os_mem_chunk->size;
		}
		set_bit(os_mem_chunk->numa_id, &os->numa_mask);

		printk(KERN_INFO "IHK-SMP: chunk 0x%lx - 0x%lx"
			   " (len: %lu) @ NUMA node: %d is assigned to OS %p\n",
			   os_mem_chunk->addr, os_mem_chunk->addr + os_mem_chunk->size,
			   os_mem_chunk->size, numa_id, ihk_os);
	}

	ret = 0;

out:
	/* Release all chunks which were to be assigned (error path) */
	list_for_each_entry_safe(os_mem_chunk_tba_iter, os_mem_chunk_tba_next,
			&to_be_assigned_chunks, list) {

		list_del(&os_mem_chunk_tba_iter->list);
		os_mem_chunk = os_mem_chunk_tba_iter;

		mem_chunk_leftover = (struct chunk*)phys_to_virt(os_mem_chunk->addr);
		mem_chunk_leftover->addr = os_mem_chunk->addr;
		mem_chunk_leftover->size = os_mem_chunk->size;
		mem_chunk_leftover->numa_id = os_mem_chunk->numa_id;

		add_free_mem_chunk(mem_chunk_leftover);
		merge_mem_chunks(&ihk_mem_free_chunks);
		kfree(os_mem_chunk);
	}

	return ret;
}

static int smp_ihk_os_assign_mem(ihk_os_t ihk_os, void *priv, unsigned long arg)
{
	size_t mem_size;
	int numa_id;
	struct smp_os_data *os = priv;
	unsigned long flags;
	int ret = 0;
	char *mem_string;
	char *mem_token;
	ihk_resource_req_t req;
	char *req_string = NULL;

	spin_lock_irqsave(&os->lock, flags);
	if (os->status != BUILTIN_OS_STATUS_INITIAL) {
		spin_unlock_irqrestore(&os->lock, flags);
		return -EBUSY;
	}
	spin_unlock_irqrestore(&os->lock, flags);

	if (copy_from_user(&req, (void *)arg, sizeof(req))) {
		printk("%s: error: copying request\n", __FUNCTION__);
		return -EFAULT;
	}

	if (req.string_len == 0) {
		printk("%s: invalid request length\n", __FUNCTION__);
		return -EINVAL;
	}

	req_string = kmalloc(req.string_len + 1, GFP_KERNEL);
	if (!req_string) {
		printk("%s: error: allocating request string\n", __FUNCTION__);
		return -EINVAL;
	}

	if (copy_from_user(req_string, req.string, req.string_len + 1)) {
		printk("%s: error: copying request string\n", __FUNCTION__);
		ret = -EFAULT;
		goto out;
	}

	mem_string = req_string;
	mem_token = strsep(&mem_string, ",");
	while (mem_token) {

		ret = smp_ihk_parse_mem(mem_token, &mem_size, &numa_id);
		if (ret != 0) {
			printk("IHK-SMP: os_assign_mem: error: parsing memory string\n");
			goto out;
		}

		ret = __smp_ihk_os_assign_mem(ihk_os, os, mem_size, numa_id);
		if (ret != 0) {
			printk("IHK-SMP: os_assign_mem: error: assigning memory chunk\n");
			goto out;
		}

		mem_token = strsep(&mem_string, ",");
	}

out:
	if (req_string) kfree(req_string);
	return ret;
}

static int smp_ihk_os_release_mem(ihk_os_t ihk_os, void *priv, unsigned long arg)
{
	struct smp_os_data *os = priv;
	unsigned long flags;
	size_t mem_size;
	int numa_id;
	int ret = -EINVAL, ret_internal;
	char *mem_string;
	char *mem_token;
	ihk_resource_req_t req;
	char *req_string = NULL;
	struct ihk_os_mem_chunk *os_mem_chunk = NULL;
	struct ihk_os_mem_chunk *next_chunk = NULL;
	struct chunk *mem_chunk;

	spin_lock_irqsave(&os->lock, flags);
	if (os->status != BUILTIN_OS_STATUS_INITIAL) {
		spin_unlock_irqrestore(&os->lock, flags);
		return -EBUSY;
	}
	spin_unlock_irqrestore(&os->lock, flags);

	ret_internal = copy_from_user(&req, (void *)arg, sizeof(req));
	ARCHDRV_CHKANDJUMP(ret_internal != 0, "copy_from_user failed", -EFAULT);

	ARCHDRV_CHKANDJUMP(req.string_len == 0, "invalid request length", -EINVAL);

	req_string = kmalloc(req.string_len + 1, GFP_KERNEL);
	ARCHDRV_CHKANDJUMP(req_string == NULL, "kmalloc failed", -EINVAL);

	ret_internal = copy_from_user(req_string, req.string, req.string_len + 1);
	ARCHDRV_CHKANDJUMP(ret_internal != 0, "copy_from_user failed", -EFAULT);

	mem_string = req_string;

	/* Drop specified memory chunks */
	mem_token = strsep(&mem_string, ",");
	while (mem_token) {
		ret_internal = smp_ihk_parse_mem(mem_token, &mem_size, &numa_id);
		ARCHDRV_CHKANDJUMP(ret_internal != 0, "smp_ihk_parse_mem failed", -EINVAL);

		list_for_each_entry_safe(os_mem_chunk, next_chunk,
								 &ihk_mem_used_chunks, list) {
			
			if (os_mem_chunk->os != ihk_os || os_mem_chunk->size != mem_size || os_mem_chunk->numa_id != numa_id) {
				continue;
			}
			
			list_del(&os_mem_chunk->list);
			
			mem_chunk = (struct chunk*)phys_to_virt(os_mem_chunk->addr);
			mem_chunk->addr = os_mem_chunk->addr;
			mem_chunk->size = os_mem_chunk->size;
			mem_chunk->numa_id = os_mem_chunk->numa_id;
			INIT_LIST_HEAD(&mem_chunk->chain);
			
			printk(KERN_INFO "IHK-SMP: chunk 0x%lx - 0x%lx"
				   " (len: %lu) @ NUMA node: %d is returned to IHK\n",
				   mem_chunk->addr, mem_chunk->addr + mem_chunk->size,
				   mem_chunk->size, mem_chunk->numa_id);
			
			add_free_mem_chunk(mem_chunk);
			
			kfree(os_mem_chunk);
			ret = 0;
			goto fn_exit;
		}
        mem_token = strsep(&mem_string, ",");
	}

 fn_exit:
	if (req_string) {
		kfree(req_string);
	}
	return ret;
 fn_fail:
	goto fn_exit;
}

static int smp_ihk_os_query_mem(ihk_os_t ihk_os, void *priv, unsigned long arg)
{
	int q_len = 0;
	struct ihk_os_mem_chunk *os_mem_chunk;

	memset(query_res, 0, sizeof(query_res));

	/* Collect memory information */
	list_for_each_entry(os_mem_chunk, &ihk_mem_used_chunks, list) {
		if (os_mem_chunk->os != ihk_os) 
			continue;

		if (q_len) {
			q_len += sprintf(query_res + q_len, ",%lu@%d",
					os_mem_chunk->size, os_mem_chunk->numa_id);
		}
		else {
			q_len = sprintf(query_res, "%lu@%d",
					os_mem_chunk->size, os_mem_chunk->numa_id);
		}
	}

	if (strlen(query_res) > 0) {
		if (copy_to_user((char *)arg, query_res, strlen(query_res) + 1)) {
			printk("IHK-SMP: error: copying mem string to user-space\n");
			return -EINVAL;
		}
	}

	return 0;
}

static int smp_ihk_os_freeze(ihk_os_t ihk_os, void *priv)
{
	smp_ihk_os_send_nmi(ihk_os, priv, 1);
	return 0;
}

static int smp_ihk_os_thaw(ihk_os_t ihk_os, void *priv)
{
	smp_ihk_os_send_nmi(ihk_os, priv, 2);
	return 0;
}

static struct ihk_os_ops smp_ihk_os_ops = {
	.load_mem = smp_ihk_os_load_mem,
	.load_file = smp_ihk_os_load_file,
	.boot = smp_ihk_os_boot,
	.shutdown = smp_ihk_os_shutdown,
	.alloc_resource = smp_ihk_os_alloc_resource,
	.query_status = smp_ihk_os_query_status,
	.wait_for_status = smp_ihk_os_wait_for_status,
	.set_kargs = smp_ihk_os_set_kargs,
	.dump = smp_ihk_os_dump,
	.issue_interrupt = smp_ihk_os_issue_interrupt,
	.map_memory = smp_ihk_os_map_memory,
	.unmap_memory = smp_ihk_os_unmap_memory,
	.register_handler = smp_ihk_os_register_handler,
	.unregister_handler = smp_ihk_os_unregister_handler,
	.get_special_addr = smp_ihk_os_get_special_addr,
	.debug_request = smp_ihk_os_debug_request,
	.get_memory_info = smp_ihk_os_get_memory_info,
	.get_cpu_info = smp_ihk_os_get_cpu_info,
	.assign_cpu = smp_ihk_os_assign_cpu,
	.release_cpu = smp_ihk_os_release_cpu,
	.ikc_map = smp_ihk_os_ikc_map,
	.query_ikc_map = smp_ihk_os_query_ikc_map,
	.query_cpu = smp_ihk_os_query_cpu,
	.assign_mem = smp_ihk_os_assign_mem,
	.release_mem = smp_ihk_os_release_mem,
	.query_mem = smp_ihk_os_query_mem,
	.freeze = smp_ihk_os_freeze,
	.thaw = smp_ihk_os_thaw,
};	

static struct ihk_register_os_data builtin_os_reg_data = {
	.name = "builtinos",
	.flag = 0,
	.ops = &smp_ihk_os_ops,
};

static int smp_ihk_create_os(ihk_device_t ihk_dev, void *priv,
                             unsigned long arg, ihk_os_t ihk_os,
                             struct ihk_register_os_data *regdata)
{
	struct builtin_device_data *data = priv;
	struct smp_os_data *os;

	*regdata = builtin_os_reg_data;

	os = kzalloc(sizeof(struct smp_os_data), GFP_KERNEL);
	if (!os) {
		data->status = 0; /* No other one should reach here */
		printk("IHK-SMP: error: allocating OS structure\n");
		return -ENOMEM;
	}

	spin_lock_init(&os->lock);
	os->dev = data;
	regdata->priv = os;
	/* Put the image into the smallest NUMA id if value is -1,
	 * use the designated NUMA node otherwise */
	os->bootstrap_numa_id = -1;

	return 0;
}

static int smp_ihk_destroy_os(ihk_device_t ihk_dev, void *ihk_dev_priv,
							  ihk_os_t ihk_os, void *ihk_os_priv)
{
	struct smp_os_data *smp_os = ihk_os_priv;
	kfree(smp_os);
	return 0;
}

/** \brief Map a remote physical memory to the local physical memory.
 *
 * In BUILTIN, all the kernels including the host kernel are running in the
 * same physical memory map, thus there is nothing to do. */
static unsigned long smp_ihk_map_memory(ihk_device_t ihk_dev, void *priv,
                                        unsigned long remote_phys,
                                        unsigned long size)
{
	/* We use the same physical memory. So no need to do something */
	return remote_phys;
}

static int smp_ihk_unmap_memory(ihk_device_t ihk_dev, void *priv,
                                unsigned long local_phys,
                                unsigned long size)
{
	return 0;
}



static void *smp_ihk_map_virtual(ihk_device_t ihk_dev, void *priv,
                                 unsigned long phys, unsigned long size,
                                 void *virt, int flags)
{
	if (!virt) {
		void *ret;

		ret = ihk_smp_map_virtual(phys, size);
		if (!ret) {
			printk("WARNING: ihk_smp_map_virtual() returned NULL!\n");
			dump_stack();
		}

		return ret;
		/*
		if (phys >= ihk_phys_start) {
			return ioremap_cache(phys, size);
		}
		else
			return 0;
		//return shimos_other_os_map(phys, size);
		*/
	}
	else {
		return ihk_host_map_generic(ihk_dev, phys, virt, size, flags);
	}
}

static int smp_ihk_unmap_virtual(ihk_device_t ihk_dev, void *priv,
                                  void *virt, unsigned long size)
{
	ihk_smp_unmap_virtual(virt);
	return 0;

	/*
	if ((unsigned long)virt >= PAGE_OFFSET) {
		iounmap(virt);
		return 0;
		//return shimos_other_os_unmap(virt, size);
	} else {
		return ihk_host_unmap_generic(ihk_dev, virt, size);
	}
	return 0;
	*/
}

static long smp_ihk_debug_request(ihk_device_t ihk_dev, void *priv,
                                  unsigned int req, unsigned long arg)
{
	return -EINVAL;
}


static int smp_ihk_init_ident_page_table(void)
{
	int ident_npages = 0;
	int i, j, k;
	unsigned long maxmem = 0, *p, physaddr;
	struct page *ident_pages;

	/* 256GB */
	maxmem = (unsigned long)256 * (1024 * 1024 * 1024);

	ident_npages = (maxmem + (1UL << PUD_SHIFT) - 1) >> PUD_SHIFT;
	ident_npages_order = fls(ident_npages + 2) - 1; 
	if ((2 << ident_npages_order) != ident_npages + 2) {
		ident_npages_order++;
	}

	printk("IHK-SMP: page table pages = %d, ident_npages_order = %d\n", 
			ident_npages, ident_npages_order);

	ident_pages = alloc_pages(GFP_DMA32 | GFP_KERNEL, ident_npages_order);
	if (!ident_pages) {
		printk("IHK-SMP: error: allocating identity page tables\n");
		return ENOMEM;
	}

	ident_page_table = page_to_phys(ident_pages);
	ident_page_table_virt = pfn_to_kaddr(page_to_pfn(ident_pages));

	memset(ident_page_table_virt, 0, ident_npages);

	/* First level : We consider only < 512 GB of memory */
	ident_page_table_virt[0] = (ident_page_table + PAGE_SIZE) | 0x63;

	/* Second level */
	p = ident_page_table_virt + (PAGE_SIZE / sizeof(*p));

	for (i = 0; i < PTRS_PER_PUD; i++) {
		if (((unsigned long)i << PUD_SHIFT) < maxmem) {
			*p = (ident_page_table + PAGE_SIZE * (2 + i)) | 0x63;
		}
		else {
			break;
		}
		p++;
	}

	if (i != ident_npages) {
		printk("Something wrong for memory map. : %d vs %d\n", i, ident_npages);
	}

	/* Third level */
	p = ident_page_table_virt + (PAGE_SIZE * 2 / sizeof(*p));
	for (j = 0; j < ident_npages; j++) {
		for (k = 0; k < PTRS_PER_PMD; k++) {
			physaddr = ((unsigned long)j << PUD_SHIFT) | 
				((unsigned long)k << PMD_SHIFT);
			if (physaddr < maxmem) {
				*p = physaddr | 0xe3;
				p++;
			}
			else {
				break;
			}
		}
	}

	printk("IHK-SMP: identity page tables allocated\n");
	return 0;
}


static irqreturn_t smp_ihk_irq_handler(int irq, void *dev_id)
{
	ack_APIC_irq();
	smp_ihk_irq_call_handlers(ihk_smp_irq, NULL);
	return IRQ_HANDLED;
}

#ifdef CONFIG_SPARSE_IRQ
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,3,0)
void (*orig_irq_flow_handler)(struct irq_desc *desc) = NULL;
void ihk_smp_irq_flow_handler(struct irq_desc *desc)
#else
void (*orig_irq_flow_handler)(unsigned int irq, struct irq_desc *desc) = NULL;
void ihk_smp_irq_flow_handler(unsigned int irq, struct irq_desc *desc)
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(4,3,0) */
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,3,0)
	unsigned int irq = desc->irq_data.irq;
#endif

	if (!desc->action || !desc->action->handler) {
		printk("IHK-SMP: no handler for IRQ %d??\n", irq);
		return;
	}
	
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,33)
	raw_spin_lock(&desc->lock);
#else
	spin_lock(&desc->lock);
#endif

	//printk("IHK-SMP: calling handler for IRQ %d\n", irq);
	desc->action->handler(irq, NULL);
	//ack_APIC_irq();
	
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,33)
	raw_spin_unlock(&desc->lock);
#else
	spin_unlock(&desc->lock);
#endif
}
#endif

#define IHK_RESERVE_PAGE_ORDER	(10)

static int __smp_ihk_free_mem_from_list(struct list_head *list)
{
	struct chunk *mem_chunk;
	struct chunk *mem_chunk_next;
	unsigned long size_left;
	unsigned long va;

	/* Drop all memory for now.. */
	list_for_each_entry_safe(mem_chunk,
			mem_chunk_next, list, chain) {
		unsigned long pa = mem_chunk->addr;
#ifdef IHK_DEBUG
		unsigned long size = mem_chunk->size;
#endif

		list_del(&mem_chunk->chain);

		va = (unsigned long)phys_to_virt(pa);
		size_left = mem_chunk->size;
		while (size_left > 0) {
			int order;
			size_t order_size;
			struct page *page = virt_to_page(va);

			if (!PageCompound(page) || !PageHead(page)) {
				printk(KERN_ERR "%s: WARNING: page is not compound or not head, skipping..\n",
					__FUNCTION__);
				size_left -= PAGE_SIZE;
				va += PAGE_SIZE;
				continue;
			}

			order = compound_order(page);
			order_size = (PAGE_SIZE << order);

			free_pages(va, order);
			pr_debug("0x%lx, page order: %d freed\n", va, order);
			/* A compound page may stretch over the size of this chunk */
			if (order_size <= size_left) {
				size_left -= order_size;
				va += order_size;
			}
			else {
				dprintk("%s: order_size - size_left: %lu\n",
					__FUNCTION__, order_size - size_left);
				size_left = 0;
			}
		}

		dprintf("IHK-SMP: 0x%lx - 0x%lx freed\n", pa, pa + size);
	}

	return 0;
}

static int __smp_ihk_free_mem_from_rbtree(struct rb_root *root)
{
	struct rb_node *node;
	struct chunk *mem_chunk;
	unsigned long size_left;
	unsigned long va;
	unsigned long pa;
#ifdef IHK_DEBUG
	unsigned long size;
#endif

	/* Drop all memory */
	node = rb_first(root);
	while (node) {
		mem_chunk = container_of(node, struct chunk, node);
		pa = mem_chunk->addr;
#ifdef IHK_DEBUG
		size = mem_chunk->size;
#endif

		rb_erase(node, root);

		va = (unsigned long)phys_to_virt(pa);
		size_left = mem_chunk->size;
		while (size_left > 0) {
			int order;
			size_t order_size;
			struct page *page = virt_to_page(va);

			if (!PageCompound(page) || !PageHead(page)) {
				printk(KERN_ERR "%s: WARNING: page is not compound or not head, skipping..\n",
					__FUNCTION__);
				size_left -= PAGE_SIZE;
				va += PAGE_SIZE;
				continue;
			}

			order = compound_order(page);
			order_size = (PAGE_SIZE << order);

			free_pages(va, order);
			pr_debug("0x%lx, page order: %d freed\n", va, order);
			/* A compound page may stretch over the size of this chunk */
			if (order_size <= size_left) {
				size_left -= order_size;
				va += order_size;
			}
			else {
				dprintk("%s: order_size - size_left: %lu\n",
					__FUNCTION__, order_size - size_left);
				size_left = 0;
			}
		}

		dprintf("IHK-SMP: 0x%lx - 0x%lx freed\n", pa, pa + size);
		node = rb_first(root);
	}

	return 0;
}


void __mem_chunk_insert(struct rb_root *root, struct chunk *chunk)
{
	struct rb_node **iter = &(root->rb_node), *parent = NULL;

	/* Figure out where to put new node */
	while (*iter) {
		struct chunk *ichunk = container_of(*iter, struct chunk, node);
		parent = *iter;

		/* Is ichunk contigous from the left? */
		if (ichunk->addr + ichunk->size == chunk->addr) {
			struct rb_node *right;
			/* Extend it to the right */
			ichunk->size += chunk->size;

			/* Have the right chunk of ichunk and ichunk become contigous? */
			right = rb_next(*iter);
			if (right) {
				struct chunk *right_chunk =
					container_of(right, struct chunk, node);

				if (ichunk->addr + ichunk->size == right_chunk->addr) {
					ichunk->size += right_chunk->size;
					rb_erase(right, root);
				}
			}

			return;
		}

		/* Is ichunk contigous from the right? */
		if (chunk->addr + chunk->size == ichunk->addr) {
			struct rb_node *left;
			/* Extend it to the left */
			ichunk->addr -= chunk->size;
			ichunk->size += chunk->size;

			/* Have the left chunk of ichunk and ichunk become contigous? */
			left = rb_prev(*iter);
			if (left) {
				struct chunk *left_chunk =
					container_of(left, struct chunk, node);

				if (left_chunk->addr + left_chunk->size == ichunk->addr) {
					ichunk->addr -= left_chunk->size;
					ichunk->size += left_chunk->size;
					rb_erase(left, root);
				}
			}

			return;
		}

		if (chunk->addr < ichunk->addr)
			iter = &((*iter)->rb_left);
		else
			iter = &((*iter)->rb_right);
	}

	/* Add new node and rebalance tree. */
	rb_link_node(&chunk->node, parent, iter);
	rb_insert_color(&chunk->node, root);

	return;
}


static int cmp_pages(void *priv, struct list_head *a, struct list_head *b)
{
	/*
	 * We just need to compare the pointers.  The 'struct
	 * page' with vmemmap are ordered in the virtual address
	 * space by physical address.  The list_head is embedded
	 * in the 'struct page'.  So we don't even have to get
	 * back to the 'struct page' here.
	 */
	if (a < b)
		return -1;
	if (a == b)
		return 0;
	/* a > b */
	return 1;
}

static void sort_pagelists(struct zone *zone)
{
	unsigned int order;
	unsigned int type;
	unsigned long flags;

	for_each_migratetype_order(order, type) {
		struct list_head *l = &zone->free_area[order].free_list[type];

		spin_lock_irqsave(&zone->lock, flags);
		list_sort(NULL, l, &cmp_pages);
		spin_unlock_irqrestore(&zone->lock, flags);
	}
}

#define RESERVE_MEM_FAILED_ATTEMPTS 20
#define RESERVE_MEM_TIMEOUT 15

int __ihk_smp_reserve_mem(size_t ihk_mem, int numa_id)
{
	int order = IHK_RESERVE_PAGE_ORDER;
	size_t want;
	size_t allocated;
	struct chunk *p;
	struct chunk *q;
	int ret = 0;
	struct rb_root tmp_chunks = RB_ROOT;
	nodemask_t nodemask;
	int i;
	unsigned long (*__try_to_free_pages)(struct zonelist *zonelist, int order,
				gfp_t gfp_mask, nodemask_t *nodemask) = NULL;
	void (*__drain_all_pages)(void) = NULL;
	int failed_free_attempts = 0;
	unsigned long res_start = get_seconds();

	memset(&nodemask, 0, sizeof(nodemask));
	__node_set(numa_id, &nodemask);

	dprintk(KERN_INFO "IHK-SMP: __ihk_smp_reserve_mem: %lu bytes\n", ihk_mem);

	__try_to_free_pages = (unsigned long (*)
			(struct zonelist *, int, gfp_t, nodemask_t *))
			kallsyms_lookup_name("try_to_free_pages");
	__drain_all_pages = (void (*)(void))
			kallsyms_lookup_name("drain_all_pages");

	if (__drain_all_pages) {
		__drain_all_pages();
	}

	/* Shrink slab/slub caches */
	{
		struct mutex *slab_mutexp =
			(struct mutex *)kallsyms_lookup_name("slab_mutex");
		struct list_head *slab_cachesp =
			(struct list_head *)kallsyms_lookup_name("slab_caches");
		if (slab_mutexp && slab_cachesp) {
			struct kmem_cache *s;

			dprintk("%s: shrinking slab caches\n", __FUNCTION__);
			mutex_lock(slab_mutexp);
			list_for_each_entry(s, slab_cachesp, list) {
				kmem_cache_shrink(s);
			}
			mutex_unlock(slab_mutexp);
		}
	}

	/* Sort page list (from Intel XPPSL patch) */
	{
		struct zone *zone;

		for (i = 0; i < MAX_NR_ZONES; i++) {
			zone = &NODE_DATA(numa_id)->node_zones[i];
			if (!zone_is_initialized(zone)) {
				continue;
			}
			if (!populated_zone(zone)) {
				continue;
			}
			dprintk("%s: sorting node %d zone %d\n",
				__FUNCTION__, numa_id, i);
			sort_pagelists(zone);
		}
	}

	want = (ihk_mem + ((PAGE_SIZE << order) - 1))
		& ~((PAGE_SIZE << order) - 1);
	dprintk("%s: ihk_mem: %lu, want: %lu\n", __FUNCTION__, ihk_mem, want);
	allocated = 0;

retry:
	/* Allocate and merge pages until we get a contigous area
	 * or run out of free memory. Keep the longest areas */
	while (max_size_mem_chunk(&tmp_chunks) < want) {
		struct page *pg;

		pg = __alloc_pages_nodemask(
				GFP_KERNEL | __GFP_COMP | __GFP_NOWARN |
				__GFP_NORETRY,
				//| __GFP_REPEAT,
				order,
				node_zonelist(numa_id, GFP_KERNEL | __GFP_COMP), &nodemask);
		if (!pg) {
			int freed_pages;

			if (__drain_all_pages) {
				__drain_all_pages();
			}

			if (__try_to_free_pages &&
					failed_free_attempts < RESERVE_MEM_FAILED_ATTEMPTS) {

				freed_pages = __try_to_free_pages(
						node_zonelist(numa_id, GFP_KERNEL),
						order,
						GFP_KERNEL, NULL);

				if (freed_pages <= 1)
					++failed_free_attempts;

				dprintk("%s: freed %d pages with order %d..\n",
						__FUNCTION__, freed_pages, order);
				goto retry;
			}

			/*
			 * We ran out of memory using the current order of compound
			 * pages, decrease order and try to grab smaller pieces.
			 */
			if (order > 1) {
				--order;
				failed_free_attempts = 0;
				dprintk("%s: order decreased to %d\n", __FUNCTION__, order);

				/* Do not spend more than RESERVE_MEM_TIMEOUT
				 * secs on reservation */
				if ((get_seconds() - res_start) < RESERVE_MEM_TIMEOUT) {
					goto retry;
				}
			}

			/*
			 * Otherwise, we may have run out of memory altogether before
			 * finding a single contigous chunk, but do we have enough in
			 * multiple chunks?
			 */
			if (allocated >= want) break;

			printk(KERN_ERR "IHK-SMP: error: __alloc_pages_node() failed\n");

			ret = -1;
			goto out;
		}

		failed_free_attempts = 0;

		p = page_address(pg);

		allocated += PAGE_SIZE << order;

		p->addr = virt_to_phys(p);
		p->size = PAGE_SIZE << order;
		p->numa_id = numa_id;
		INIT_LIST_HEAD(&p->chain);

		__mem_chunk_insert(&tmp_chunks, p);
	}

	dprintk("%s: allocated internally: %lu\n", __FUNCTION__, allocated);

	/* Move the largest chunks to free list until we meet the required size */
	allocated = 0;
	while (allocated < want) {
		struct rb_node *node;
		size_t max = 0;
		p = NULL;

		for (node = rb_first(&tmp_chunks); node; node = rb_next(node)) {
			struct chunk *q = container_of(node, struct chunk, node);
			if (q->size > max) {
				p = q;
				max = p->size;
			}
		}

		if (!p) break;

		rb_erase(&p->node, &tmp_chunks);

		/* Verify that chunk structure is in front of physical memory */
		if (page_to_phys(virt_to_page(p)) != p->addr) {
			struct chunk *__p = (struct chunk *)phys_to_virt(p->addr);
			*__p = *p;
			p = __p;
			dprintk("%s: moved chunk structure to front of 0x%lx\n",
				__FUNCTION__, p->addr);
		}

		/* Insert the chunk in physical address ascending order */
		list_for_each_entry(q, &ihk_mem_free_chunks, chain) {
			if (p->addr < q->addr) {
				break;
			}
		}

		if ((void *)q == &ihk_mem_free_chunks) {
			list_add_tail(&p->chain, &ihk_mem_free_chunks);
		}
		else {
			list_add_tail(&p->chain, &q->chain);
		}

		printk(KERN_INFO "IHK-SMP: chunk 0x%lx - 0x%lx"
				" (len: %lu) @ NUMA node: %d is available\n",
				p->addr, p->addr + p->size, p->size, p->numa_id);
		allocated += max;
	}

	ret = 0;

out:
	/* Free leftover tmp_chunks */
	__smp_ihk_free_mem_from_rbtree(&tmp_chunks);

	return ret;
}

static int __ihk_smp_release_mem(size_t ihk_mem, int numa_id)
{
	int ret = -1;
	struct chunk *mem_chunk;
	struct chunk *mem_chunk_next;
	unsigned long size_left;
	unsigned long va;

	list_for_each_entry_safe(mem_chunk,
			mem_chunk_next, &ihk_mem_free_chunks, chain) {
		unsigned long pa = mem_chunk->addr;

		if(mem_chunk->size != ihk_mem || mem_chunk->numa_id != numa_id) {
			continue;
		}

		list_del(&mem_chunk->chain);

		va = (unsigned long)phys_to_virt(pa);
		size_left = mem_chunk->size;
		while (size_left > 0) {
			int order;
			size_t order_size;
			struct page *page = virt_to_page(va);

			if (!PageCompound(page) || !PageHead(page)) {
				printk(KERN_ERR "%s: WARNING: page is not compound or not head, skipping..\n",
					__FUNCTION__);
				size_left -= PAGE_SIZE;
				va += PAGE_SIZE;
				continue;
			}

			order = compound_order(page);
			order_size = (PAGE_SIZE << order);

			free_pages(va, order);
			pr_debug("0x%lx, page order: %d freed\n", va, order);
			/* A compound page may stretch over the size of this chunk */
			if (order_size <= size_left) {
				size_left -= order_size;
				va += order_size;
			}
			else {
				dprintk("%s: order_size - size_left: %lu\n",
					__FUNCTION__, order_size - size_left);
				size_left = 0;
			}
		}

		printk(KERN_INFO "IHK-SMP: chunk 0x%lx - 0x%lx"
				" (len: %lu) @ NUMA node: %d is released\n",
			   mem_chunk->addr, mem_chunk->addr + mem_chunk->size,
			   mem_chunk->size, mem_chunk->numa_id);

		ret = 0;
		goto fn_exit;
	}

 fn_exit:
	return ret;
}

static int _smp_ihk_write_cpu_sys_file(int cpu_id, char *val)
{
	struct file* filp = NULL;
	mm_segment_t oldfs;
	int ret, err = 0;
	char path[256];

	sprintf(path, "/sys/devices/system/cpu/cpu%d/online", cpu_id);

	oldfs = get_fs();

	set_fs(get_ds());
	filp = filp_open(path, O_RDWR, 0);
	if (IS_ERR(filp)) {
		set_fs(oldfs);
		err = PTR_ERR(filp);
		printk("%s: error opening %s\n", __FUNCTION__, path);
		return -1;
	}

	ret = kernel_write(filp, val, 1, 0);
	if (ret != 1) {
		filp_close(filp, NULL);
		set_fs(oldfs);
		printk("%s: error writing %s\n", __FUNCTION__, path);
		return -1;
	}

	filp_close(filp, NULL);
	set_fs(oldfs);
	return 0;
}


static int smp_ihk_offline_cpu(int cpu_id)
{
	return _smp_ihk_write_cpu_sys_file(cpu_id, "0");
}

static int smp_ihk_online_cpu(int cpu_id)
{
	return _smp_ihk_write_cpu_sys_file(cpu_id, "1");
}

static int smp_ihk_reserve_cpu(ihk_device_t ihk_dev, unsigned long arg)
{
	int ret;
	int cpu;
	cpumask_t cpus_to_offline;
	ihk_resource_req_t req;
	char *req_string = NULL;

	if (copy_from_user(&req, (void *)arg, sizeof(req))) {
		printk("%s: error: copying request\n", __FUNCTION__);
		return -EFAULT;
	}

	if (req.string_len == 0) {
		printk("%s: invalid request length\n", __FUNCTION__);
		return -EINVAL;
	}

	req_string = kmalloc(req.string_len + 1, GFP_KERNEL);
	if (!req_string) {
		printk("%s: error: allocating request string\n", __FUNCTION__);
		return -EINVAL;
	}

	if (copy_from_user(req_string, req.string, req.string_len + 1)) {
		printk("%s: error: copying request string\n", __FUNCTION__);
		ret = -EFAULT;
		goto out;
	}

	memset(&cpus_to_offline, 0, sizeof(cpus_to_offline));

	/* Parse CPU list provided by user */
	if (cpulist_parse(req_string, &cpus_to_offline) < 0) {
		printk("%s: invalid CPUs requested\n", __FUNCTION__);
		ret = -EINVAL;
		goto out;
	}

	/* Ugly, but for_each_cpu doesn't look beyond nr_cpu_ids */
	for (cpu = nr_cpu_ids;
			cpu < sizeof(cpus_to_offline) * BITS_PER_BYTE; ++cpu) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,1,0)
		if (cpumask_test_cpu(cpu, &cpus_to_offline)) {
#else
		if (cpu_isset(cpu, cpus_to_offline)) {
#endif
			printk("%s: invalid CPU requested: %d\n",
					__FUNCTION__, cpu);
			return -EINVAL;
		}
	}

	/* Collect cores to be offlined */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,0,0)
	for_each_cpu(cpu, &cpus_to_offline) {
#else
	for_each_cpu_mask(cpu, cpus_to_offline) {
#endif
		if (cpu > SMP_MAX_CPUS) {
			printk("IHK-SMP: error: CPU %d is out of limit\n", cpu);
			ret = -EINVAL;
			goto err_before_offline;
		}

		if (!cpu_present(cpu)) {
			printk("IHK-SMP: error: CPU %d is not present\n", cpu);
			ret = -EINVAL;
			goto err_before_offline;
		}

		if (!cpu_online(cpu)) {
			if (ihk_smp_cpus[cpu].status == IHK_SMP_CPU_AVAILABLE)
				printk("IHK-SMP: error: CPU %d was reserved already\n", cpu);

			if (ihk_smp_cpus[cpu].status == IHK_SMP_CPU_ASSIGNED)
				printk("IHK-SMP: erro: CPU %d was assigned already\n", cpu);

			ret = -EINVAL;
			goto err_before_offline;
		}

		if (ihk_smp_cpus[cpu].status != IHK_SMP_CPU_ONLINE) {
			printk("IHK-SMP: error: CPU %d is in inconsistent state, skipping\n",
					cpu);
			ret = -EINVAL;
			goto err_before_offline;
		}

		ihk_smp_cpus[cpu].id = cpu;
		ihk_smp_cpus[cpu].apic_id =
			per_cpu(x86_cpu_to_apicid, ihk_smp_cpus[cpu].id);
		ihk_smp_cpus[cpu].status = IHK_SMP_CPU_TO_OFFLINE;
		ihk_smp_cpus[cpu].os = (ihk_os_t)0;

		dprintk(KERN_INFO "IHK-SMP: CPU %d to be offlined, APIC: %d\n",
			ihk_smp_cpus[cpu].id, ihk_smp_cpus[cpu].apic_id);
	}

	/* Offline CPU cores */
	for (cpu = 0; cpu < SMP_MAX_CPUS; ++cpu) {
		if (ihk_smp_cpus[cpu].status != IHK_SMP_CPU_TO_OFFLINE)
			continue;

		if ((ret = smp_ihk_offline_cpu(cpu)) != 0) {
			goto err_during_offline;
		}

		ihk_smp_cpus[cpu].apic_id =
			per_cpu(x86_cpu_to_apicid, ihk_smp_cpus[cpu].id);
		ihk_smp_cpus[cpu].status = IHK_SMP_CPU_OFFLINED;
		ihk_smp_cpus[cpu].os = (ihk_os_t)0;

		ihk_smp_reset_cpu(ihk_smp_cpus[cpu].apic_id);

		dprintk(KERN_INFO "IHK-SMP: CPU %d offlined successfully, APIC: %d\n",
			ihk_smp_cpus[cpu].id, ihk_smp_cpus[cpu].apic_id);
	}

	/* Offlining CPU cores went well, mark them as available */
	for (cpu = 0; cpu < SMP_MAX_CPUS; ++cpu) {
		if (ihk_smp_cpus[cpu].status != IHK_SMP_CPU_OFFLINED)
			continue;
		ihk_smp_cpus[cpu].status = IHK_SMP_CPU_AVAILABLE;

		dprintk(KERN_INFO "IHK-SMP: CPU %d reserved successfully, APIC: %d\n",
			ihk_smp_cpus[cpu].id, ihk_smp_cpus[cpu].apic_id);
	}

	printk(KERN_INFO "IHK-SMP: CPUs: %s reserved successfully\n", req_string);
	ret = 0;
	goto out;

err_during_offline:
	for (cpu = 0; cpu < SMP_MAX_CPUS; ++cpu) {
		if (ihk_smp_cpus[cpu].status != IHK_SMP_CPU_OFFLINED)
			continue;

		smp_ihk_online_cpu(cpu);
		ihk_smp_cpus[cpu].status = IHK_SMP_CPU_ONLINE;
	}

err_before_offline:
	for (cpu = 0; cpu < SMP_MAX_CPUS; ++cpu) {
		if (ihk_smp_cpus[cpu].status != IHK_SMP_CPU_TO_OFFLINE)
			continue;

		ihk_smp_cpus[cpu].status = IHK_SMP_CPU_ONLINE;
	}

out:
	if (req_string) kfree(req_string);
	return ret;
}

static int smp_ihk_release_cpu(ihk_device_t ihk_dev, unsigned long arg)
{
	int ret;
	int cpu;
	cpumask_t cpus_to_online;
	ihk_resource_req_t req;
	char *req_string = NULL;

	if (copy_from_user(&req, (void *)arg, sizeof(req))) {
		printk("%s: error: copying request\n", __FUNCTION__);
		return -EFAULT;
	}

	if (req.string_len == 0) {
		printk("%s: invalid request length\n", __FUNCTION__);
		return -EINVAL;
	}

	req_string = kmalloc(req.string_len + 1, GFP_KERNEL);
	if (!req_string) {
		printk("%s: error: allocating request string\n", __FUNCTION__);
		return -EINVAL;
	}

	if (copy_from_user(req_string, req.string, req.string_len + 1)) {
		printk("%s: error: copying request string\n", __FUNCTION__);
		ret = -EFAULT;
		goto out;
	}

	memset(&cpus_to_online, 0, sizeof(cpus_to_online));

	/* Parse CPU list provided by user */
	if (cpulist_parse(req_string, &cpus_to_online) < 0) {
		printk("%s: invalid CPUs requested\n", __FUNCTION__);
		ret = -EINVAL;
		goto out;
	}

	/* Collect cores to be onlined */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,0,0)
	for_each_cpu(cpu, &cpus_to_online) {
#else
	for_each_cpu_mask(cpu, cpus_to_online) {
#endif
		if (cpu > SMP_MAX_CPUS) {
			printk("IHK-SMP: error: CPU %d is out of limit\n", cpu);
			ret = -EINVAL;
			goto err;
		}

		if (!cpu_present(cpu)) {
			printk("IHK-SMP: error: CPU %d is not valid\n", cpu);
			ret = -EINVAL;
			goto err;
		}

		if (cpu_online(cpu)) {
			continue;
		}

		if (ihk_smp_cpus[cpu].status != IHK_SMP_CPU_AVAILABLE) {
			printk("IHK-SMP: error: CPU %d is in use\n", cpu);
			ret = -EINVAL;
			goto err;
		}

		ihk_smp_cpus[cpu].id = cpu;
		ihk_smp_cpus[cpu].apic_id =
			per_cpu(x86_cpu_to_apicid, ihk_smp_cpus[cpu].id);
		ihk_smp_cpus[cpu].status = IHK_SMP_CPU_TO_ONLINE;
		ihk_smp_cpus[cpu].os = (ihk_os_t)0;

		dprintk("IHK-SMP: CPU %d to be onlined, APIC: %d\n",
			ihk_smp_cpus[cpu].id, ihk_smp_cpus[cpu].apic_id);
	}

	/* Online CPU cores */
	for (cpu = 0; cpu < SMP_MAX_CPUS; ++cpu) {
		if (ihk_smp_cpus[cpu].status != IHK_SMP_CPU_TO_ONLINE)
			continue;

		if ((ret = smp_ihk_online_cpu(cpu)) != 0) {
			goto err;
		}

		ihk_smp_cpus[cpu].status = IHK_SMP_CPU_ONLINE;
		ihk_smp_cpus[cpu].os = (ihk_os_t)0;

		dprintk("IHK-SMP: CPU %d onlined successfully, APIC: %d\n",
			ihk_smp_cpus[cpu].id, ihk_smp_cpus[cpu].apic_id);
	}

	ret = 0;
	goto out;

err:
	/* Something went wrong, what shall we do?
	 * Mark "to be onlined" cores as available for now */
	for (cpu = 0; cpu < SMP_MAX_CPUS; ++cpu) {
		if (ihk_smp_cpus[cpu].status != IHK_SMP_CPU_TO_ONLINE)
			continue;

		ihk_smp_cpus[cpu].status = IHK_SMP_CPU_AVAILABLE;
	}

out:
	if (req_string) kfree(req_string);

	return ret;
}

static int smp_ihk_query_cpu(ihk_device_t ihk_dev, unsigned long arg)
{
	int cpu;
	cpumask_t cpus_reserved;

	memset(&cpus_reserved, 0, sizeof(cpus_reserved));
	memset(query_res, 0, sizeof(query_res));

	for (cpu = 0; cpu < SMP_MAX_CPUS; ++cpu) {
		if (ihk_smp_cpus[cpu].status != IHK_SMP_CPU_AVAILABLE)
			continue;

		cpumask_set_cpu(cpu, &cpus_reserved);
	}

	BITMAP_SCNLISTPRINTF(query_res, sizeof(query_res),
		cpumask_bits(&cpus_reserved), nr_cpumask_bits);

	if (strlen(query_res) > 0) {
		if (copy_to_user((char *)arg, query_res, strlen(query_res) + 1)) {
			printk("IHK-SMP: error: copying CPU string to user-space\n");
			return -EINVAL;
		}
	}

	return 0;
}

static int smp_ihk_reserve_mem(ihk_device_t ihk_dev, unsigned long arg)
{
	size_t mem_size;
	int numa_id;
	int ret = 0;
	char *mem_string;
	char *mem_token;
	ihk_resource_req_t req;
	char *req_string = NULL;

	if (copy_from_user(&req, (void *)arg, sizeof(req))) {
		printk("%s: error: copying request\n", __FUNCTION__);
		return -EFAULT;
	}

	if (req.string_len == 0) {
		printk("%s: invalid request length\n", __FUNCTION__);
		return -EINVAL;
	}

	req_string = kmalloc(req.string_len + 1, GFP_KERNEL);
	if (!req_string) {
		printk("%s: error: allocating request string\n", __FUNCTION__);
		return -EINVAL;
	}

	if (copy_from_user(req_string, req.string, req.string_len + 1)) {
		printk("%s: error: copying request string\n", __FUNCTION__);
		ret = -EFAULT;
		goto out;
	}

	mem_string = req_string;

	/* Check mem size */
	mem_token = strsep(&mem_string, ",");
	while (mem_token) {
		ret = smp_ihk_parse_mem(mem_token, &mem_size, &numa_id);
		if (ret != 0) {
			printk("IHK-SMP: reserve_mem: error: parsing memory string\n");
			break;
		}

		if (mem_size % (1024 * 1024 * 4) != 0) {
			printk("%s: error: mem_size must be in multiples of %d bytes\n",
					__FUNCTION__, 1024 * 1024 * 4);
			ret = -1;
			break;
		}

		mem_token = strsep(&mem_string, ",");
	}

	if (ret != 0) {
		goto out;
	}

	if (copy_from_user(req_string, req.string, req.string_len + 1)) {
		printk("%s: error: copying request string\n", __FUNCTION__);
		ret = -EFAULT;
		goto out;
	}

	mem_string = req_string;

	/* Do the reservation */
	mem_token = strsep(&mem_string, ",");
	while (mem_token) {
		smp_ihk_parse_mem(mem_token, &mem_size, &numa_id);
		ret = __ihk_smp_reserve_mem(mem_size, numa_id);
		if (ret != 0) {
			printk("IHK-SMP: reserve_mem: error: reserving memory\n");
			break;
		}

		mem_token = strsep(&mem_string, ",");
	}

out:
	if (req_string) kfree(req_string);

	return ret;
}

static int smp_ihk_release_mem(ihk_device_t ihk_dev, unsigned long arg)
{
	size_t mem_size;
	int numa_id;
	int ret = 0, ret_internal;
	char *mem_string;
	char *mem_token;
	ihk_resource_req_t req;
	char *req_string = NULL;

	ret_internal = copy_from_user(&req, (void *)arg, sizeof(req));
	ARCHDRV_CHKANDJUMP(ret_internal != 0, "copy_from_user failed", -EFAULT);

	ARCHDRV_CHKANDJUMP(req.string_len == 0, "invalid request length", -EINVAL);

	req_string = kmalloc(req.string_len + 1, GFP_KERNEL);
	ARCHDRV_CHKANDJUMP(req_string == NULL, "kmalloc failed", -EINVAL);

	ret_internal = copy_from_user(req_string, req.string, req.string_len + 1);
	ARCHDRV_CHKANDJUMP(ret_internal != 0, "copy_from_user failed", -EFAULT);

	mem_string = req_string;

	/* Do the release */
	mem_token = strsep(&mem_string, ",");
	while (mem_token) {
		ret_internal = smp_ihk_parse_mem(mem_token, &mem_size, &numa_id);
		ARCHDRV_CHKANDJUMP(ret_internal != 0, "smp_ihk_parse_mem failed", -EINVAL);

		ret_internal = __ihk_smp_release_mem(mem_size, numa_id);
		/* ret = __smp_ihk_free_mem_from_list(&ihk_mem_free_chunks); */
		ARCHDRV_CHKANDJUMP(ret_internal != 0, "__ihk_smp_release_mem failed", -EINVAL);

        mem_token = strsep(&mem_string, ",");
	}
 fn_fail:
	if (req_string) {
		kfree(req_string);
	}

	return ret;
}

static int smp_ihk_query_mem(ihk_device_t ihk_dev, unsigned long arg)
{
	int q_len = 0;
	struct chunk *mem_chunk;

	memset(query_res, 0, sizeof(query_res));

	/* Collect memory information */
	list_for_each_entry(mem_chunk, &ihk_mem_free_chunks, chain) {

		if (q_len) {
			q_len += sprintf(query_res + q_len, ",%lu@%d",
					mem_chunk->size, mem_chunk->numa_id);
		}
		else {
			q_len = sprintf(query_res, "%lu@%d",
					mem_chunk->size, mem_chunk->numa_id);
		}
	}

	if (strlen(query_res) > 0) {
		if (copy_to_user((char *)arg, query_res, strlen(query_res) + 1)) {
			printk("IHK-SMP: error: copying mem string to user-space\n");
			return -EINVAL;
		}
	}

	return 0;
}

static LIST_HEAD(cpu_topology_list);
static LIST_HEAD(node_topology_list);

static void free_info(void)
{
	struct ihk_cpu_topology *cpu;
	struct ihk_cpu_topology *nextcpu;
	struct ihk_cache_topology *cache;
	struct ihk_cache_topology *nextcache;
	struct ihk_node_topology *node;
	struct ihk_node_topology *nextnode;

	dprintk("free_info()\n");
	list_for_each_entry_safe(cpu, nextcpu, &cpu_topology_list, chain) {
		list_del(&cpu->chain);

		list_for_each_entry_safe(cache, nextcache,
				&cpu->cache_topology_list, chain) {
			list_del(&cache->chain);

			kfree(cache->type);
			kfree(cache->size_str);
			kfree(cache);
		}

		kfree(cpu);
	}

	list_for_each_entry_safe(node, nextnode, &node_topology_list, chain) {
		list_del(&node->chain);

		kfree(node);
	}

	dprintk("free_info():\n");
	return;
} /* free_info() */

static int read_file(void *buf, size_t size, char *fmt, va_list ap)
{
	int error;
	int er;
	char *filename = NULL;
	int n;
	struct file *fp = NULL;
	loff_t off;
	mm_segment_t ofs;
	ssize_t ss;

	dprintk("read_file(%p,%ld,%s,%p)\n", buf, size, fmt, ap);
	filename = kmalloc(PATH_MAX, GFP_KERNEL);
	if (!filename) {
		error = -ENOMEM;
		eprintk("ihk:read_file:kmalloc failed. %d\n", error);
		goto out;
	}

	n = vsnprintf(filename, PATH_MAX, fmt, ap);
	if (n >= PATH_MAX) {
		error = -ENAMETOOLONG;
		eprintk("ihk:read_file:vsnprintf failed. %d\n", error);
		goto out;
	}

	fp = filp_open(filename, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		error = PTR_ERR(fp);
		eprintk("ihk:read_file:filp_open failed. %d\n", error);
		goto out;
	}

	off = 0;
	ofs = get_fs();
	set_fs(KERNEL_DS);
	ss = vfs_read(fp, buf, size, &off);
	set_fs(ofs);
	if (ss < 0) {
		error = ss;
		eprintk("ihk:read_file:vfs_read failed. %d\n", error);
		goto out;
	}
	if (ss >= size) {
		error = -ENOSPC;
		eprintk("ihk:read_file:buffer overflow. %d\n", error);
		goto out;
	}
	*(char *)(buf + ss) = '\0';

	error = 0;
out:
	if (!IS_ERR_OR_NULL(fp)) {
		er = filp_close(fp, NULL);
		if (er) {
			eprintk("ihk:read_file:filp_close failed. %d\n", er);
		}
	}
	kfree(filename);
	dprintk("read_file(%p,%ld,%s,%p): %d\n", buf, size, fmt, ap, error);
	return error;
} /* read_file() */

static int file_readable(char *fmt, ...)
{
	int ret;
	va_list ap;
	int n;
	char *filename = NULL;
	struct file *fp = NULL;

	filename = kmalloc(PATH_MAX, GFP_KERNEL);
	if (!filename) {
		eprintk("%s: kmalloc failed. %d\n",
				__FUNCTION__, -ENOMEM);
		ret = 0;
		goto out;
	}

	va_start(ap, fmt);
	n = vsnprintf(filename, PATH_MAX, fmt, ap);
	va_end(ap);

	if (n >= PATH_MAX) {
		eprintk("%s: vsnprintf failed. %d\n",
				__FUNCTION__, -ENAMETOOLONG);
		ret = 0;
		goto out;
	}

	fp = filp_open(filename, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		ret = 0;
		goto out;
	}

	filp_close(fp, NULL);
	ret = 1;

out:
	kfree(filename);
	return ret;
}

static int read_long(long *valuep, char *fmt, ...)
{
	int error;
	char *buf = NULL;
	va_list ap;
	int n;

	dprintk("read_long(%p,%s)\n", valuep, fmt);
	buf = (void *)__get_free_pages(GFP_KERNEL, 0);
	if (!buf) {
		error = -ENOMEM;
		eprintk("ihk:read_long:__get_free_pages failed. %d\n", error);
		goto out;
	}

	va_start(ap, fmt);
	error = read_file(buf, PAGE_SIZE, fmt, ap);
	va_end(ap);
	if (error) {
		eprintk("ihk:read_long:read_file failed. %d\n", error);
		goto out;
	}

	n = sscanf(buf, "%ld", valuep);
	if (n != 1) {
		error = -EIO;
		eprintk("ihk:read_long:sscanf failed. %d\n", error);
		goto out;
	}

	error = 0;
out:
	free_pages((long)buf, 0);
	dprintk("read_long(%p,%s): %d\n", valuep, fmt, error);
	return error;
} /* read_long() */

static int read_bitmap(void *map, int nbits, char *fmt, ...)
{
	int error;
	char *buf = NULL;
	va_list ap;

	dprintk("read_bitmap(%p,%d,%s)\n", map, nbits, fmt);
	buf = (void *)__get_free_pages(GFP_KERNEL, 0);
	if (!buf) {
		error = -ENOMEM;
		eprintk("ihk:read_bitmap:__get_free_pages failed. %d\n", error);
		goto out;
	}

	va_start(ap, fmt);
	error = read_file(buf, PAGE_SIZE, fmt, ap);
	va_end(ap);
	if (error) {
		eprintk("ihk:read_bitmap:read_file failed. %d\n", error);
		goto out;
	}

	error = bitmap_parse(buf, PAGE_SIZE, map, nbits);
	if (error) {
		eprintk("ihk:read_bitmap:bitmap_parse failed. %d\n", error);
		goto out;
	}

	error = 0;
out:
	free_pages((long)buf, 0);
	dprintk("read_bitmap(%p,%d,%s): %d\n", map, nbits, fmt, error);
	return error;
} /* read_bitmap() */

static int read_string(char **valuep, char *fmt, ...)
{
	int error;
	char *buf = NULL;
	va_list ap;
	char *p = NULL;
	int len;

	dprintk("read_string(%p,%s)\n", valuep, fmt);
	buf = (void *)__get_free_pages(GFP_KERNEL, 0);
	if (!buf) {
		error = -ENOMEM;
		eprintk("ihk:read_string:"
				"__get_free_pages failed. %d\n", error);
		goto out;
	}

	va_start(ap, fmt);
	error = read_file(buf, PAGE_SIZE, fmt, ap);
	va_end(ap);
	if (error) {
		eprintk("ihk:read_string:read_file failed. %d\n", error);
		goto out;
	}

	p = kstrdup(buf, GFP_KERNEL);
	if (!p) {
		error = -ENOMEM;
		eprintk("ihk:read_string:kstrdup failed. %d\n", error);
		goto out;
	}

	len = strlen(p);
	if (len && (p[len-1] == '\n')) {
		p[len-1] = '\0';
	}

	error = 0;
	*valuep = p;
	p = NULL;

out:
	kfree(p);
	free_pages((long)buf, 0);
	dprintk("read_string(%p,%s): %d\n", valuep, fmt, error);
	return error;
} /* read_string() */

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
	if (error) {
		eprintk("ihk:collect_cache_topology:"
				"read_long(level) failed. %d\n", error);
		goto out;
	}

	error = read_string(&p->type, "%s/type", prefix);
	if (error) {
		eprintk("ihk:collect_cache_topology:"
				"read_string(type) failed. %d\n", error);
		goto out;
	}

	error = read_long(&p->size, "%s/size", prefix);
	if (error) {
		eprintk("ihk:collect_cache_topology:"
				"read_long(size) failed. %d\n", error);
		goto out;
	}
	p->size *= 1024;	/* XXX */

	error = read_string(&p->size_str, "%s/size", prefix);
	if (error) {
		eprintk("ihk:collect_cache_topology:"
				"read_string(size) failed. %d\n", error);
		goto out;
	}

	error = read_long(&p->coherency_line_size,
			"%s/coherency_line_size", prefix);
	if (error) {
		eprintk("ihk:collect_cache_topology:"
				"read_long(coherency_line_size) failed. %d\n",
				error);
		goto out;
	}

	error = read_long(&p->number_of_sets, "%s/number_of_sets", prefix);
	if (error) {
		eprintk("ihk:collect_cache_topology:"
				"read_long(number_of_sets) failed. %d\n",
				error);
		goto out;
	}

	error = read_long(&p->physical_line_partition,
			"%s/physical_line_partition", prefix);
	if (error) {
		eprintk("ihk:collect_cache_topology:"
				"read_long(physical_line_partition) failed."
				" %d\n", error);
		goto out;
	}

	error = read_long(&p->ways_of_associativity,
			"%s/ways_of_associativity", prefix);
	if (error) {
		eprintk("ihk:collect_cache_topology:"
				"read_long(ways_of_associativity) failed."
				" %d\n", error);
		goto out;
	}

	error = read_bitmap(&p->shared_cpu_map, nr_cpumask_bits,
			"%s/shared_cpu_map", prefix);
	if (error) {
		eprintk("ihk:collect_cache_topology:"
				"read_bitmap(shared_cpu_map) failed. %d\n",
				error);
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
	p->hw_id = per_cpu(x86_cpu_to_apicid, cpu);

	error = read_long(&p->core_id, "%s/topology/core_id", prefix);
	if (error) {
		eprintk("ihk:collect_cpu_info:"
				"read_long(core_id) failed. %d\n", error);
		goto out;
	}

	error = read_bitmap(&p->core_siblings, nr_cpumask_bits,
			"%s/topology/core_siblings", prefix);
	if (error) {
		eprintk("ihk:collect_cpu_info:"
				"read_bitmap(core_siblings) failed. %d\n",
				error);
		goto out;
	}

	error = read_long(&p->physical_package_id,
			"%s/topology/physical_package_id", prefix);
	if (error) {
		eprintk("ihk:collect_cpu_info:"
				"read_long(physical_package_id) failed. %d\n",
				error);
		goto out;
	}

	error = read_bitmap(&p->thread_siblings, nr_cpumask_bits,
			"%s/topology/thread_siblings", prefix);
	if (error) {
		eprintk("ihk:collect_cpu_info:"
				"read_bitmap(thread_siblings) failed. %d\n",
				error);
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
		goto out;
	}

	error = 0;
	list_add(&p->chain, &node_topology_list);
	p = NULL;

out:
	kfree(p);
	dprintk("collect_node_topology(%d): %d\n", node, error);
	return error;
} /* collect_node_topology() */

static int collect_topology(void)
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

static struct ihk_cpu_topology *smp_ihk_get_cpu_topology(ihk_device_t dev, void *priv, int hw_id)
{
	struct ihk_cpu_topology *topo;

	dprintk("smp_ihk_get_cpu_topology(%p,%p,%#x)\n", dev, priv, hw_id);
	list_for_each_entry(topo, &cpu_topology_list, chain) {
		if (topo->hw_id == hw_id) {
			goto out;
		}
	}
	topo = NULL;
out:
	dprintk("smp_ihk_get_cpu_topology(%p,%p,%#x): %p\n", dev, priv, hw_id, topo);
	return topo;
} /* smp_ihk_get_cpu_topology() */

static struct ihk_node_topology *smp_ihk_get_node_topology(ihk_device_t dev, void *priv, int node)
{
	struct ihk_node_topology *topo;

	dprintk("smp_ihk_get_node_topology(%p,%p,%d)\n", dev, priv, node);
	if (node >= nr_node_ids) {
		topo = ERR_PTR(-EINVAL);
		goto out;
	}

	list_for_each_entry(topo, &node_topology_list, chain) {
		if (topo->node_number == node) {
			goto out;
		}
	}
	topo = NULL;
out:
	dprintk("smp_ihk_get_node_topology(%p,%p,%d): %p\n", dev, priv, node, topo);
	return topo;
} /* smp_ihk_get_node_topology() */

static int smp_ihk_linux_cpu_to_hw_id(ihk_device_t dev, void *priv, int cpu)
{
	struct ihk_cpu_topology *topo;
	int hw_id;

	dprintk("smp_ihk_linux_cpu_to_hw_id(%p,%p,%d)\n", dev, priv, cpu);
	list_for_each_entry(topo, &cpu_topology_list, chain) {
		if (topo->cpu_number == cpu) {
			hw_id = topo->hw_id;
			goto out;
		}
	}
	hw_id = -1;
out:
	dprintk("smp_ihk_linux_cpu_to_hw_id(%p,%p,%d): %#x\n", dev, priv, cpu, hw_id);
	return hw_id;
} /* smp_ihk_linux_cpu_to_hw_id() */

static struct irq_chip ihk_irq_chip = {
	.name = "ihk_irq",
};

static int
vector_is_used(int vector, int core) {
	int rtn = 0;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,3,0)
	/* As of 4.3.0, vector_irq is an array of struct irq_desc pointers */
	struct irq_desc **vectors = (*SHIFT_PERCPU_PTR((vector_irq_t *)_vector_irq,
					per_cpu_offset(core)));
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,0)
/* TODO: find out where exactly between 2.6.32 and 3.0.0 vector_irq was changed */
	int *vectors = (*SHIFT_PERCPU_PTR((vector_irq_t *)_vector_irq,
				per_cpu_offset(core)));
#else
	int *vectors = (*SHIFT_PERCPU_PTR((vector_irq_t *)_per_cpu__vector_irq,
				per_cpu_offset(core)));
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(4,3,0) */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,3,0)
	if (vectors[vector] != VECTOR_UNUSED) {
		printk(KERN_INFO "IHK-SMP: IRQ vector %d in core %d: used %d \n",
				vector, core, vectors[vector]);
		rtn = 1;
	}
#else
	if (vectors[vector] != -1) {
		printk(KERN_INFO "IHK-SMP: IRQ vector %d in core %d: used %d \n",
				vector, core, vectors[vector]);
		rtn = 1;
	}
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(4,3,0) */
	return rtn;
}

static void
set_vector(int vector, int core) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,3,0)
	/* As of 4.3.0, vector_irq is an array of struct irq_desc pointers */
	struct irq_desc **vectors = (*SHIFT_PERCPU_PTR((vector_irq_t *)_vector_irq,
						per_cpu_offset(core)));
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,0)
	int *vectors = (*SHIFT_PERCPU_PTR((vector_irq_t *)_vector_irq,
				per_cpu_offset(core)));
#else
	int *vectors = (*SHIFT_PERCPU_PTR((vector_irq_t *)_per_cpu__vector_irq,
		per_cpu_offset(core)));
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,3,0)
	if (vectors[vector] == VECTOR_UNUSED) {
		dprintk(KERN_INFO "IHK-SMP: fixed vector_irq for %d in core %d\n", vector, core);
		vectors[vector] = desc;
	}
#else
	if (vectors[vector] == -1) {
		dprintk(KERN_INFO "IHK-SMP: fixed vector_irq for %d in core %d\n", vector, core);
		vectors[vector] = vector;
	}
#endif
}

static void
release_vector(int vector, int core) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,3,0)
	/* As of 4.3.0, vector_irq is an array of struct irq_desc pointers */
	struct irq_desc **vectors = (*SHIFT_PERCPU_PTR((vector_irq_t *)_vector_irq,
				per_cpu_offset(core)));
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,0)
	int *vectors = (*SHIFT_PERCPU_PTR((vector_irq_t *)_vector_irq,
				per_cpu_offset(core)));
#else
	int *vectors = (*SHIFT_PERCPU_PTR((vector_irq_t *)_per_cpu__vector_irq,
				per_cpu_offset(core)));
#endif

	/* Release IRQ vector */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,3,0)
	vectors[vector] = VECTOR_UNUSED;
#else
	vectors[vector] = -1;
#endif
}

static int smp_ihk_init(ihk_device_t ihk_dev, void *priv)
{
	int error = 0;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,2,0)
	int vector = ISA_IRQ_VECTOR(15) + 2;
#else
	int vector = IRQ15_VECTOR + 2;
#endif
	int i = 0;
	int is_used = 0;

	INIT_LIST_HEAD(&ihk_mem_free_chunks);
	INIT_LIST_HEAD(&ihk_mem_used_chunks);

	if (ihk_cores) {
		if (ihk_cores > (num_present_cpus() - 1)) {
			printk("IHK-SMP error: only %d CPUs in total are available\n", 
					num_present_cpus());
			return EINVAL;	
		}
	}

	if (ihk_trampoline) {
		printk("IHK-SMP: preallocated trampoline phys: 0x%lx\n", ihk_trampoline);
		
		trampoline_phys = ihk_trampoline;
		trampoline_va = ioremap_cache(trampoline_phys, PAGE_SIZE);

	}
	else {
#define TRAMP_ATTEMPTS	20
		int attempts = 0;
		struct page *bad_pages[TRAMP_ATTEMPTS];
		
		memset(bad_pages, 0, TRAMP_ATTEMPTS * sizeof(struct page *));

		/* Try to allocate trampoline page, it has to be under 1M so we can 
		 * execute real-mode AP code. If allocation fails more than 
		 * TRAMP_ATTEMPTS times, we will use Linux's one.
		 * NOTE: using Linux trampoline could potentially cause race 
		 * conditions with concurrent CPU onlining requests */
retry_trampoline:
		trampoline_page = alloc_pages(GFP_DMA | GFP_KERNEL, 1);

		if (!trampoline_page || page_to_phys(trampoline_page) > 0xFF000) {
			bad_pages[attempts] = trampoline_page;
			
			if (++attempts < TRAMP_ATTEMPTS) {
				goto retry_trampoline;
			}
		}

		/* Free failed attempts.. */
		for (attempts = 0; attempts < TRAMP_ATTEMPTS; ++attempts) {
			if (!bad_pages[attempts]) {
				continue;
			}

			free_pages((unsigned long)pfn_to_kaddr(page_to_pfn(bad_pages[attempts])), 1);
		}

		/* Couldn't allocate trampoline page, use Linux' one from real_header */
		if (!trampoline_page || page_to_phys(trampoline_page) > 0xFF000) {
			using_linux_trampoline = 1;
			printk("IHK-SMP: warning: allocating trampoline_page failed, using Linux'\n");
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,38))
			trampoline_phys = TRAMPOLINE_BASE;
#elif ((LINUX_VERSION_CODE > KERNEL_VERSION(2,6,38)) && (LINUX_VERSION_CODE < KERNEL_VERSION(3,4,0)))
#ifdef IHK_KSYM_x86_trampoline_base
#if IHK_KSYM_x86_trampoline_base
			trampoline_phys = __pa(x86_trampoline_base);
#endif
#endif
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,0))
#ifdef IHK_KSYM_real_mode_header
#if IHK_KSYM_real_mode_header
			trampoline_phys = real_mode_header->trampoline_start;
#endif
#endif
#endif /* LINUX_VERSION_CODE check */
			trampoline_va = __va(trampoline_phys);
		}
		else {
			trampoline_phys = page_to_phys(trampoline_page);
			trampoline_va = pfn_to_kaddr(page_to_pfn(trampoline_page));
		}

		printk(KERN_INFO "IHK-SMP: trampoline_page phys: 0x%lx\n", trampoline_phys);
	}

	memset(ihk_smp_cpus, 0, sizeof(ihk_smp_cpus));

	/* Find a suitable IRQ vector */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,2,0)
	for (vector = ihk_start_irq ? ihk_start_irq : (ISA_IRQ_VECTOR(14) + 2); 
			vector < 256; vector += 1) {
#else
	for (vector = ihk_start_irq ? ihk_start_irq : (IRQ14_VECTOR + 2); 
			vector < 256; vector += 1) {
#endif	
#ifdef CONFIG_SPARSE_IRQ
		struct irq_desc *desc;
#endif

		if (test_bit(vector, used_vectors)) {
			printk(KERN_INFO "IHK-SMP: IRQ vector %d: used\n", vector);
			continue;
		}

		for (i = 0; i < nr_cpu_ids; i++) {
			if (vector_is_used(vector, i)) {
				is_used = 1;
				break;
			}
		}

		if (is_used) {
			is_used = 0;
			continue;
		}

#ifdef CONFIG_SPARSE_IRQ
		/* If no descriptor, create one */
		desc = _irq_to_desc(vector);
		if (!desc) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0)
			desc = _alloc_desc(vector, first_online_node, THIS_MODULE);
			desc->irq_data.chip = _dummy_irq_chip;
			radix_tree_insert(_irq_desc_tree, vector, desc);
#else
			desc = _irq_to_desc_alloc_node(vector, first_online_node);
			if (!desc) {
				printk(KERN_INFO "IHK-SMP: IRQ vector %d: failed allocating descriptor\n", vector);
				continue;
			}
			desc->chip = _dummy_irq_chip;
#endif
		}
		
		desc = _irq_to_desc(vector);
		if (!desc) {
			printk(KERN_INFO "IHK-SMP: IRQ vector %d: no descriptor\n", vector);
			continue;
		}

		if (desc->action) {
			// action is already registered.
			continue;
		}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0)
		if (desc->status_use_accessors & IRQ_NOREQUEST) {
			
			printk(KERN_INFO "IHK-SMP: IRQ vector %d: not allowed to request, fake it\n", vector);
			
			desc->status_use_accessors &= ~IRQ_NOREQUEST;
		}
#else
		if (desc->status & IRQ_NOREQUEST) {
			
			printk(KERN_INFO "IHK-SMP: IRQ vector %d: not allowed to request, fake it\n", vector);
			
			desc->status &= ~IRQ_NOREQUEST;
		}
#endif
		orig_irq_flow_handler = desc->handle_irq;
		desc->handle_irq = ihk_smp_irq_flow_handler;
#endif // CONFIG_SPARSE_IRQ

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,1,0))
#define IRQF_DISABLED 0x0
#endif
		if (request_irq(vector, 
					smp_ihk_irq_handler, IRQF_DISABLED, "IHK-SMP", NULL) != 0) {
			printk(KERN_INFO "IHK-SMP: IRQ vector %d: request_irq failed\n", vector);
			continue;
		}

		/* Pretend a real external interrupt */
		for (i = 0; i < nr_cpu_ids; i++) {
			set_vector(vector, i);
		}
		break;
	}

	if (vector >= 256) {
		printk(KERN_ERR "IHK-SMP: error: allocating IKC irq vector\n");
		error = EFAULT;
		goto error_free_trampoline;
	}

	ihk_smp_irq = vector;
	ihk_smp_irq_apicid = (int)per_cpu(x86_bios_cpu_apicid, 
		ihk_ikc_irq_core);
	printk(KERN_INFO "IHK-SMP: IKC irq vector: %d, CPU logical id: %u, CPU APIC id: %d\n",
	    ihk_smp_irq, ihk_ikc_irq_core, ihk_smp_irq_apicid);

	irq_set_chip(vector, &ihk_irq_chip);
	irq_set_chip_data(vector, NULL);

	error = smp_ihk_init_ident_page_table();
	if (error) {
		printk(KERN_ERR "IHK-SMP: error: identity page table initialization failed\n");
		goto error_free_irq;
	}

	error = collect_topology();
	if (error) {
		printk(KERN_ERR "IHK-SMP: error: collecting topology information failed\n");
		goto error_free_irq;
	}

#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,0)) && \
		(LINUX_VERSION_CODE <= KERNEL_VERSION(4,3,5)))
	/* NOTE: this is nasty, but we need to decrease the refcount because
	 * after Linux 3.0 request_irq holds an extra reference to the module. 
	 * This causes rmmod to fail and report the module is in use when one
	 * tries to unload it. To overcome this, we drop one ref here and get
	 * an extra one before free_irq in the module's exit code */
	if (module_refcount(THIS_MODULE) == 2) {
		module_put(THIS_MODULE);
		this_module_put = 1;
	}
#endif

	return error;

error_free_irq:
#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,0)) && \
	(LINUX_VERSION_CODE <= KERNEL_VERSION(4,3,0)))
	if (this_module_put) {
		try_module_get(THIS_MODULE);
	}
#endif

	free_irq(ihk_smp_irq, NULL);

error_free_trampoline:
	if (trampoline_page) {
		free_pages((unsigned long)pfn_to_kaddr(page_to_pfn(trampoline_page)), 1);
	}
	else {
		if (!using_linux_trampoline)
			iounmap(trampoline_va);
	}

	return error;
}

int ihk_smp_reset_cpu(int phys_apicid) {
	unsigned long send_status;
	int maxlvt;

	preempt_disable();
	dprintk(KERN_INFO "IHK-SMP: resetting CPU %d.\n", phys_apicid);

	maxlvt = _lapic_get_maxlvt();

	/*
	 * Be paranoid about clearing APIC errors.
	 */
	if (APIC_INTEGRATED(apic_version[phys_apicid])) {
		if (maxlvt > 3)         /* Due to the Pentium erratum 3AP.  */
			apic_write(APIC_ESR, 0);
		apic_read(APIC_ESR);
	}

	pr_debug("Asserting INIT.\n");

	/*
	 * Turn INIT on target chip
	 */
	/*
	 * Send IPI
	 */
	apic_icr_write(APIC_INT_LEVELTRIG | APIC_INT_ASSERT | APIC_DM_INIT,
			phys_apicid);

	pr_debug("Waiting for send to finish...\n");
	send_status = safe_apic_wait_icr_idle();

	mdelay(10);

	pr_debug("Deasserting INIT.\n");

	/* Target chip */
	/* Send IPI */
	apic_icr_write(APIC_INT_LEVELTRIG | APIC_DM_INIT, phys_apicid);

	pr_debug("Waiting for send to finish...\n");
	send_status = safe_apic_wait_icr_idle();

	preempt_enable();
	return 0;
}


static int smp_ihk_exit(ihk_device_t ihk_dev, void *priv) 
{
	int cpu;
	int i = 0;

#ifdef CONFIG_SPARSE_IRQ
	struct irq_desc *desc;
#endif

	/* Release IRQ vector */
	for (i = 0; i < nr_cpu_ids; i++) {
		release_vector(ihk_smp_irq, i);
	}

	irq_set_chip(ihk_smp_irq, NULL);

#ifdef CONFIG_SPARSE_IRQ
	desc = _irq_to_desc(ihk_smp_irq);
	desc->handle_irq = orig_irq_flow_handler;
#endif

#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,0)) && \
	(LINUX_VERSION_CODE <= KERNEL_VERSION(4,3,0)))
	if (this_module_put) {
		try_module_get(THIS_MODULE);
	}
#endif

	free_irq(ihk_smp_irq, NULL);
	
#ifdef CONFIG_SPARSE_IRQ
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,0)
	irq_free_descs(ihk_smp_irq, 1);
#endif
#endif

	/* Re-enable CPU cores */
	for (cpu = 0; cpu < SMP_MAX_CPUS; ++cpu) {
		if (ihk_smp_cpus[cpu].status == IHK_SMP_CPU_ONLINE)
			continue;

		ihk_smp_reset_cpu(ihk_smp_cpus[cpu].apic_id);

		if (smp_ihk_online_cpu(cpu) != 0) {
			continue;
		}

		printk("IHK-SMP: CPU %d onlined successfully, APIC: %d\n", 
			ihk_smp_cpus[cpu].id, ihk_smp_cpus[cpu].apic_id);
	}

	if (trampoline_page) {
		free_pages((unsigned long)pfn_to_kaddr(page_to_pfn(trampoline_page)), 1);
	}
	else {
		if (!using_linux_trampoline)
			iounmap(trampoline_va);
	}

	if (ident_npages_order) {
		free_pages((unsigned long)ident_page_table_virt, ident_npages_order);
	}

	/* Free memory */
	__smp_ihk_free_mem_from_list(&ihk_mem_free_chunks);

	free_info();

	return 0;
}

static struct ihk_device_ops smp_ihk_device_ops = {
	.init = smp_ihk_init,
	.exit = smp_ihk_exit,
	.create_os = smp_ihk_create_os,
	.destroy_os = smp_ihk_destroy_os,
	.map_memory = smp_ihk_map_memory,
	.unmap_memory = smp_ihk_unmap_memory,
	.map_virtual = smp_ihk_map_virtual,
	.unmap_virtual = smp_ihk_unmap_virtual,
	.debug_request = smp_ihk_debug_request,
	.get_dma_channel = smp_ihk_get_dma_channel,
	.reserve_cpu = smp_ihk_reserve_cpu,
	.release_cpu = smp_ihk_release_cpu,
	.reserve_mem = smp_ihk_reserve_mem,
	.release_mem = smp_ihk_release_mem,
	.query_cpu = smp_ihk_query_cpu,
	.query_mem = smp_ihk_query_mem,
	.get_cpu_topology = smp_ihk_get_cpu_topology,
	.get_node_topology = smp_ihk_get_node_topology,
	.linux_cpu_to_hw_id = smp_ihk_linux_cpu_to_hw_id,
};	

/** \brief The driver-specific driver structure
 *
 * Since there is only one BUILTIN "device" in machine, this structure is
 * statically allocated. */
static struct builtin_device_data builtin_data;

static struct ihk_register_device_data builtin_dev_reg_data = {
	.name = "SMP",
	.flag = 0,
	.priv = &builtin_data,
	.ops = &smp_ihk_device_ops,
};

static int __init smp_module_init(void)
{
	ihk_device_t ihkd;

	printk(KERN_INFO "IHK-SMP: initializing...\n");

	spin_lock_init(&builtin_data.lock);

	if (!(ihkd = ihk_register_device(&builtin_dev_reg_data))) {
		printk(KERN_INFO "builtin: Failed to register ihk driver.\n");
		return -ENOMEM;
	}

	builtin_data.ihk_dev = ihkd;
	
	return 0;
}

static void __exit smp_module_exit(void)
{
	printk(KERN_INFO "IHK-SMP: finalizing...\n");
	ihk_unregister_device(builtin_data.ihk_dev);
}

module_init(smp_module_init);
module_exit(smp_module_exit);

MODULE_LICENSE("Dual BSD/GPL");
