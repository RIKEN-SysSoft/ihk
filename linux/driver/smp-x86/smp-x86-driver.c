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
#include <linux/version.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/fs.h>
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
#include <linux/irq.h>
#if LINUX_VERSION_CODE == KERNEL_VERSION(2,6,32)
#include <linux/autoconf.h>
#endif
#include <ihk/ihk_host_driver.h>
#include <ihk/ihk_host_misc.h>
#include <ihk/ihk_host_user.h>
#define IHK_DEBUG
#include <ihk/misc/debug.h>
#include <ikc/msg.h>
//#include <linux/shimos.h>
//#include "builtin_dma.h"
#include <asm/ipi.h>
#include <asm/uv/uv.h>
#include <asm/nmi.h>
#include <asm/tlbflush.h>
#include <asm/mc146818rtc.h>
#include <asm/smpboot_hooks.h>
#include "bootparam.h"

#define BUILTIN_OS_STATUS_INITIAL  0
#define BUILTIN_OS_STATUS_LOADING  1
#define BUILTIN_OS_STATUS_LOADED   2
#define BUILTIN_OS_STATUS_BOOTING  3

//#define BUILTIN_MAX_CPUS SHIMOS_MAX_CORES

#define BUILTIN_COM_VECTOR  0xf1

#define LARGE_PAGE_SIZE	(1UL << 21)
#define LARGE_PAGE_MASK	(~((unsigned long)LARGE_PAGE_SIZE - 1))

#define MAP_ST_START	0xffff800000000000UL
#define MAP_KERNEL_START	0xffffffff80000000UL

#define PTL4_SHIFT	39
#define PTL3_SHIFT	30
#define PTL2_SHIFT	21


/*
 * IHK-SMP unexported kernel symbols
 */
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

#ifdef IHK_KSYM_init_deasserted
#if IHK_KSYM_init_deasserted
atomic_t *_init_deasserted = 
	(atomic_t *)
	IHK_KSYM_init_deasserted;
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
struct irq_desc *(*_alloc_desc)(int irq, int node, struct module *owner) =
	IHK_KSYM_alloc_desc;
#endif
#endif

#ifdef IHK_KSYM_irq_desc_tree
#if IHK_KSYM_irq_desc_tree
struct radix_tree_root *_irq_desc_tree = 
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
#endif

#ifdef IHK_KSYM_wakeup_secondary_cpu_via_init
#if IHK_KSYM_wakeup_secondary_cpu_via_init
int (*_wakeup_secondary_cpu_via_init)(int phys_apicid, 
	unsigned long start_eip) = 
	IHK_KSYM_wakeup_secondary_cpu_via_init;
#endif
#endif

#ifdef IHK_KSYM_cpu_up
#if IHK_KSYM_cpu_up
typedef int (*int_star_fn_int_t)(unsigned int);
int (*_cpu_up)(unsigned int cpu) =
	(int_star_fn_int_t)
	IHK_KSYM_cpu_up;
#else // exported
int (*_cpu_up)(unsigned int cpu) = cpu_up;
#endif
#endif

#ifdef IHK_KSYM_cpu_hotplug_driver_lock 
#if IHK_KSYM_cpu_hotplug_driver_lock 
void (*_cpu_hotplug_driver_lock)(void) = 
	IHK_KSYM_cpu_hotplug_driver_lock;
#else // exported
#include <linux/cpu.h>
void (*_cpu_hotplug_driver_lock)(void) = 
	cpu_hotplug_driver_lock;
#endif
#else // static
#include <linux/cpu.h>
void (*_cpu_hotplug_driver_lock)(void) = 
	cpu_hotplug_driver_lock;
#endif	

#ifdef IHK_KSYM_cpu_hotplug_driver_unlock
#if IHK_KSYM_cpu_hotplug_driver_unlock
void (*_cpu_hotplug_driver_unlock)(void) = 
	IHK_KSYM_cpu_hotplug_driver_unlock;
#else // exported
void (*_cpu_hotplug_driver_unlock)(void) = 
	cpu_hotplug_driver_unlock;
#endif
#else // static
void (*_cpu_hotplug_driver_unlock)(void) = 
	cpu_hotplug_driver_unlock;
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


#define IHK_SMP_MAXCPUS	256

#define BUILTIN_MAX_CPUS IHK_SMP_MAXCPUS

struct cpu_id {
	int id;
	int apic_id;
};

int cpus_requested = 2;
static struct cpu_id reserved_cpu_ids[IHK_SMP_MAXCPUS];
struct page *trampoline_page;

unsigned long ident_page_table;
int ident_npages_order = 0;
unsigned long *ident_page_table_virt;

int ihk_smp_irq = 0;
int ihk_smp_irq_apicid = 0;

int ihk_smp_reset_cpu(int phys_apicid);

extern const char ihk_smp_trampoline_end[], ihk_smp_trampoline_data[];
#define IHK_SMP_TRAMPOLINE_SIZE \
	roundup(ihk_smp_trampoline_end - ihk_smp_trampoline_data, PAGE_SIZE)

/* ----------------------------------------------- */

/** \brief BUILTIN boot parameter structure
 *
 * This structure contains vairous parameters both passed to the manycore 
 * kernel, and passed from the manycore kernel.
 */
struct builtin_boot_param {
	/** \brief SHIMOS-specific boot parameters. Memory start, end etc.
	 * (passed to the manycore) */
	struct shimos_boot_param bp;

	/** \brief Manycore-physical address of the kernel message buffer
	 * of the manycore kernel (filled by the manycore) */
	unsigned long msg_buffer;
	/** \brief Manycore physical address of the receive queue of 
	 * the master IKC channel (filled by the manycore) */
	unsigned long mikc_queue_recv;
	/** \brief Manycore physical address of the send queue of 
	 * the master IKC channel (filled by the manycore) */
	unsigned long mikc_queue_send;

	/** \brief Host physical address of the DMA structure
	 * (passed to the manycore) */
	unsigned long dma_address;
	/** \brief Host physical address of the identity-mapped page table
	 * (passed to the manycore) */
	unsigned long ident_table;

	/** \brief Kernel command-line parameter */
	char kernel_args[256];
};

/** \brief BUILTIN driver-specific OS structure */
struct builtin_os_data {
	/** \brief Lock for this structure */
	spinlock_t lock;

	/** \brief Pointer to the device structure */
	struct builtin_device_data *dev;
	/** \brief Allocated CPU core mask */
	shimos_coreset coremaps;
	/** \brief Start address of the allocated memory region */
	unsigned long mem_start;
	/** \brief End address of the allocated memory region */
	unsigned long mem_end;

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
	int cpu_hw_ids[BUILTIN_MAX_CPUS];

	/** \brief Kernel command-line parameter.
	 *
	 * This will be copied to boot_param just before booting so that
	 * it does not change while the kernel is running.
	 */
	char kernel_args[256];

	/** \brief Boot parameter for the kernel
	 *
	 * This structure is directly accessed (read and written)
	 * by the manycore kernel. */
	struct builtin_boot_param param;

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
	uintptr_t addr;
	size_t size;
};

/* ihk_os_mem_chunk represents a memory range which is used by 
 * one of the OSs */
struct ihk_os_mem_chunk {
	struct list_head list;
	uintptr_t addr;
	size_t size;
	ihk_os_t os;
};

static struct list_head ihk_mem_free_chunks;
static struct list_head unused;
static struct list_head ihk_mem_used_chunks;

void *ihk_smp_map_virtual(unsigned long phys, unsigned long size)
{
	if (ihk_mem) {

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
	}

	else if (ihk_phys_start) {
		return ioremap_cache(phys, size);
	}

	return 0;
}

void ihk_smp_unmap_virtual(void *virt)
{
	if (ihk_mem) {
		/* TODO: look up chunks and report error if not in range */
		return;	
	}
	else if (ihk_phys_start) {
		iounmap(virt);
	}
}

/** \brief Implementation of ihk_host_get_dma_channel.
 *
 * It returns the information of the only channel in the DMA emulating core. */
static ihk_dma_channel_t builtin_ihk_get_dma_channel(ihk_device_t dev, void *priv,
                                                 int channel)
{
	return NULL;
}

/** \brief Set the status member of the OS data with lock */
static void set_os_status(struct builtin_os_data *os, int status)
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
static void __build_os_info(struct builtin_os_data *os)
{
	int i, c;

	os->mem_info.n_mappable = os->mem_info.n_available = 1;
	os->mem_info.n_fixed = 0;
	os->mem_info.available = os->mem_info.mappable = &os->mem_region;
	os->mem_info.fixed = NULL;
	os->mem_region.start = os->mem_start;
	os->mem_region.size = os->mem_end - os->mem_start;
	
	for (i = 0, c = 0; i < BUILTIN_MAX_CPUS; i++) {
		if (CORE_ISSET(i, os->coremaps)) {
			os->cpu_hw_ids[c] = i;
			c++;
		}
	}
	os->cpu_info.n_cpus = c;
	os->cpu_info.hw_ids = os->cpu_hw_ids;
}

struct ihk_smp_trampoline_header{
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
	unsigned long flags;

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
	local_irq_save(flags);
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
	local_irq_restore(flags);

	mb();
	atomic_set(_init_deasserted, 1);

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
		local_irq_save(flags);
		apic_icr_write(APIC_DM_STARTUP | (start_eip >> 12),
			       phys_apicid);

		/*
		 * Give the other CPU some time to accept the IPI.
		 */
		udelay(300);

		pr_debug("Startup point 1.\n");

		pr_debug("Waiting for send to finish...\n");
		send_status = safe_apic_wait_icr_idle();
		local_irq_restore(flags);

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
	int boot_error;

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
		printk("calling apic->wakeup_secondary_cpu()\n");
		return apic->wakeup_secondary_cpu(apicid, start_eip);
	}
	else {
		printk("calling smp_wakeup_secondary_cpu_via_init()\n");
		return smp_wakeup_secondary_cpu_via_init(apicid, start_eip);
	}
}

/** \brief Boot a kernel. */
static int builtin_ihk_os_boot(ihk_os_t ihk_os, void *priv, int flag)
{
	struct builtin_os_data *os = priv;
	struct builtin_device_data *dev = os->dev;
	unsigned long flags;
	struct ihk_smp_trampoline_header *header;

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

	memset(&os->param, 0, sizeof(os->param));
	os->param.bp.start = os->mem_start;
	os->param.bp.end = os->mem_end;
	os->param.bp.coreset = os->coremaps;
	os->param.ident_table = ident_page_table;
	strncpy(os->param.kernel_args, os->kernel_args,
	        sizeof(os->param.kernel_args));

	dprintf("boot cpu : %d, %lx, %lx, %lx, %lx\n",
	        os->boot_cpu, os->mem_start, os->mem_end, os->coremaps.set[0],
	        os->param.dma_address
	);

	/* Prepare trampoline code */
	memcpy(pfn_to_kaddr(page_to_pfn(trampoline_page)), 
			ihk_smp_trampoline_data,
			IHK_SMP_TRAMPOLINE_SIZE);

	header = pfn_to_kaddr(page_to_pfn(trampoline_page));
	header->page_table = ident_page_table;
	header->next_ip = os->boot_rip;
	header->notify_address = __pa(&os->param.bp);
	
	printk("calling wakeup_secondary_cpu...\n");
	udelay(300);
	
	return smp_wakeup_secondary_cpu(os->boot_cpu, page_to_phys(trampoline_page));
}

static int
builtin_ihk_os_load_file(ihk_os_t ihk_os, void *priv, const char *fn)
{
	struct builtin_os_data *os = priv;
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

	if (!CORE_ISSET_ANY(&os->coremaps) || os->mem_end - os->mem_start < 0) {
		printk("builtin: OS is not ready to boot.\n");
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
	elf64 = ihk_smp_map_virtual(os->mem_end - PAGE_SIZE, PAGE_SIZE); 
	if (!elf64) {
		printk("error: ioremap() returns NULL\n");
		return -EINVAL;
	}
	fs = get_fs();
	set_fs(get_ds());
printk("read pa=%lx va=%lx\n", os->mem_end - PAGE_SIZE, (unsigned long)elf64);
	r = vfs_read(file, (char *)elf64, PAGE_SIZE, &pos);
	set_fs(fs);
	if (r <= 0) {
		printk("vfs_read failed: %ld\n", r);
		ihk_smp_unmap_virtual(elf64);
		//iounmap(elf64);
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
		//iounmap(elf64);
		fput(file);
		return (int)-EINVAL;
	}
	entry = elf64->e_entry;
	elf64p = (Elf64_Phdr *)(((char *)elf64) + elf64->e_phoff);
	phys = (os->mem_start + LARGE_PAGE_SIZE * 2 - 1) & LARGE_PAGE_MASK;
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

			if(l > PAGE_SIZE)
				l = PAGE_SIZE;
			if (offset + PAGE_SIZE > os->mem_end) {
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
		for(size = (size + PAGE_SIZE - 1) & PAGE_MASK; size < psize; size += PAGE_SIZE){

			if (offset + PAGE_SIZE > os->mem_end) {
				printk("builtin: OS is too big to load.\n");
				return -E2BIG;
			}
			buf = ihk_smp_map_virtual(offset, PAGE_SIZE); 
			memset(buf, '\0', PAGE_SIZE);
			ihk_smp_unmap_virtual(buf);
			offset += PAGE_SIZE;
		}
		if(offset > maxoffset)
			maxoffset = offset;
	}
	fput(file);
	ihk_smp_unmap_virtual(elf64);

	pml4_p = os->mem_end - PAGE_SIZE;
	pdp_p = pml4_p - PAGE_SIZE;
	pde_p = pdp_p - PAGE_SIZE;


	cr3 = ident_page_table_virt;
	pml4 = ihk_smp_map_virtual(pml4_p, PAGE_SIZE); 
	pdp = ihk_smp_map_virtual(pdp_p, PAGE_SIZE); 
	pde = ihk_smp_map_virtual(pde_p, PAGE_SIZE); 

	memset(pml4, '\0', PAGE_SIZE);
	memset(pdp, '\0', PAGE_SIZE);
	memset(pde, '\0', PAGE_SIZE);

	pml4[0] = cr3[0];
	pml4[(MAP_ST_START >> PTL4_SHIFT) & 511] = cr3[0];
	pml4[(MAP_KERNEL_START >> PTL4_SHIFT) & 511] = pdp_p | 3;
	pdp[(MAP_KERNEL_START >> PTL3_SHIFT) & 511] = pde_p | 3;
	n = (os->mem_end - os->mem_start) >> PTL2_SHIFT;
	if(n > 511)
		n = 511;

	for (i = 0; i < n; i++) {
		pde[i] = (phys + (i << PTL2_SHIFT)) | 0x83;
	}
	pde[511] = (os->mem_end - (2 << PTL2_SHIFT)) | 0x83;

	ihk_smp_unmap_virtual(pde);
	ihk_smp_unmap_virtual(pdp);
	ihk_smp_unmap_virtual(pml4);

	startup_p = os->mem_end - (2 << PTL2_SHIFT);
	startup = ihk_smp_map_virtual(startup_p, PAGE_SIZE);
	memcpy(startup, startup_data, startup_data_end - startup_data);
	startup[2] = pml4_p;
	startup[3] = 0xffffffffc0000000;
	startup[4] = phys;
	startup[5] = page_to_phys(trampoline_page);
	startup[6] = (unsigned long)ihk_smp_irq | 
		((unsigned long)ihk_smp_irq_apicid << 32);
	startup[7] = entry;
	ihk_smp_unmap_virtual(startup);
	os->boot_rip = startup_p;

	set_os_status(os, BUILTIN_OS_STATUS_INITIAL);
	return 0;
}

static int builtin_ihk_os_load_mem(ihk_os_t ihk_os, void *priv, const char *buf,
                               unsigned long size, long offset)
{
	struct builtin_os_data *os = priv;
	unsigned long phys, to_read, flags;
	void *virt;

	dprint_func_enter;

	/* We just load from the lowest address of the private memory */
	if (!CORE_ISSET_ANY(&os->coremaps) || os->mem_end - os->mem_start < 0) {
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

static int builtin_ihk_os_shutdown(ihk_os_t ihk_os, void *priv, int flag)
{
	struct builtin_os_data *os = priv;
	int i, apicid;
	struct ihk_os_mem_chunk *os_mem_chunk = NULL;
	struct chunk *mem_chunk;
	
	for (i = 0; i < cpus_requested; ++i) {
		ihk_smp_reset_cpu(reserved_cpu_ids[i].apic_id);

		printk("IHK-SMP: CPU %d has been re-set successfully, APIC: %d\n", 
			reserved_cpu_ids[i].id, reserved_cpu_ids[i].apic_id);
	}

	/* Drop memory chunk used by this OS */
	list_for_each_entry(os_mem_chunk, &ihk_mem_used_chunks, list) {
		if (os_mem_chunk->os == ihk_os) {
			list_del(&os_mem_chunk->list);

			mem_chunk = (struct chunk*)phys_to_virt(os_mem_chunk->addr);
			mem_chunk->addr = os_mem_chunk->addr;
			mem_chunk->size = os_mem_chunk->size;
			INIT_LIST_HEAD(&mem_chunk->chain);

			list_add_tail(&mem_chunk->chain, &ihk_mem_free_chunks);

			printk("IHK-SMP: mem chunk: 0x%lx - 0x%lx freed\n",
				mem_chunk->addr, mem_chunk->addr + mem_chunk->size);

			kfree(os_mem_chunk);
			break;
		}
	}

	
	os->status = BUILTIN_OS_STATUS_INITIAL;

#if 0
	for (i = BUILTIN_MAX_CPUS - 1; i >= 0; i--) {
		if (CORE_ISSET(i, os->coremaps)) {
			shimos_reset_cpu(i);

			apicid = i;
			shimos_free_cpus(1, &apicid);
		}
	}

	spin_lock_irqsave(&os->lock, flags);
	CORE_ZERO(os->coremaps);
	st = os->mem_start;
	ed = os->mem_end;
	os->mem_start = os->mem_end = 0;
	os->status = BUILTIN_OS_STATUS_INITIAL;
	spin_unlock_irqrestore(&os->lock, flags);

	shimos_free_memory(st, ed - st);
#endif	

	return 0;
}


static int builtin_ihk_os_alloc_resource(ihk_os_t ihk_os, void *priv,
                                     struct ihk_resource *resource)
{
	struct builtin_os_data *os = priv;
	int apicids[BUILTIN_MAX_CPUS];
	int i, n, ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&os->lock, flags);
	if (os->status != BUILTIN_OS_STATUS_INITIAL) {
		spin_unlock_irqrestore(&os->lock, flags);
		return -EBUSY;
	}
	os->status = BUILTIN_OS_STATUS_LOADING;
	spin_unlock_irqrestore(&os->lock, flags);

	if (resource->cpu_cores) {
#if 0	
		if (resource->cpu_cores > BUILTIN_MAX_CPUS) {
			ret = -EINVAL;
		} else if (resource->flags & IHK_RESOURCE_FLAG_CPU_SPECIFIED) {
			n = resource->cpu_cores;
			if (shimos_reserve_cpus(resource->cpu_cores, 
			                        resource->cores) == 0) {
				for (i = 0; i < n; i++) {
					CORE_SET(resource->cores[i], os->coremaps);
				}
 			} else {
				ret = -ENOMEM;
			}
		} 
		else 
#endif		
		{
			/*
			n = shimos_allocate_cpus(resource->cpu_cores, apicids);
			for (i = 0; i < n; i++) {
				if (apicids[i] < BUILTIN_MAX_CPUS) {
					dprintf("BUILTIN: Core %d allocated.\n",
					        apicids[i]);
					CORE_SET(apicids[i], os->coremaps);
				}
			}
			*/

			if (resource->cpu_cores > cpus_requested) {
				printk("IHK-SMP: error: %d CPUs requested, but only %d available\n",
					resource->cpu_cores, cpus_requested);
			}

			n = resource->cpu_cores;

			for (i = 0; i < n; i++) {
				if (reserved_cpu_ids[i].apic_id < BUILTIN_MAX_CPUS) {
					printk("IHK-SMP: Core APIC %d allocated.\n",
					        reserved_cpu_ids[i].apic_id);
					CORE_SET(reserved_cpu_ids[i].apic_id, os->coremaps);
				}
			}
			if (n <= 0) {
				ret = -ENOMEM;
			}
		}
	}

	/* TODO: When we allocate more than an area... */
	if (!ret && resource->mem_size) {
		struct ihk_os_mem_chunk *os_mem_chunk;
		struct chunk *mem_chunk_iter;
		os_mem_chunk = kmalloc(sizeof(struct ihk_os_mem_chunk), GFP_KERNEL);
		
		if (!os_mem_chunk) {
			printk("IHK-DMP: error: allocating os_mem_chunk\n");
			return -ENOMEM;
		}
		
		os_mem_chunk->addr = 0;
		INIT_LIST_HEAD(&os_mem_chunk->list);
#if 0
		if (resource->flags & IHK_RESOURCE_FLAG_MEM_SPECIFIED) {
			if (shimos_reserve_memory(resource->mem_start,
			                          resource->mem_size)) {
				ret = -ENOMEM;
			}
		} else if (shimos_allocate_memory(resource->mem_size,
		                                  &resource->mem_start)) {
			ret = -ENOMEM;
		}
#endif

		if (ihk_mem) {
			list_for_each_entry(mem_chunk_iter, &ihk_mem_free_chunks, chain) {
				if (mem_chunk_iter->size >= resource->mem_size) {

					os_mem_chunk->addr = mem_chunk_iter->addr;
					os_mem_chunk->size = mem_chunk_iter->size;
					os_mem_chunk->os = ihk_os;

					list_del(&mem_chunk_iter->chain);
					break;
				}
			}

			if (!os_mem_chunk->addr) {
				printk("IHK-SMP: error: not enough memory\n");
				return -ENOMEM;
			}

			list_add(&os_mem_chunk->list, &ihk_mem_used_chunks);

			resource->mem_start = os_mem_chunk->addr;
		}
		else if (ihk_phys_start) {
			resource->mem_start = ihk_phys_start;
		}

		if (!ret) { /* If successfully allocated ... */
			os->mem_start = resource->mem_start;
			os->mem_end = os->mem_start + resource->mem_size;

			dprintf("IHK-SMP-x86: Memory %lx - %lx allocated.\n",
			        os->mem_start, os->mem_end);
		}
	}

	set_os_status(os, BUILTIN_OS_STATUS_INITIAL);
	return ret;
}

static enum ihk_os_status builtin_ihk_os_query_status(ihk_os_t ihk_os, void *priv)
{
	struct builtin_os_data *os = priv;
	int status;

	status = os->status;

	if (status == BUILTIN_OS_STATUS_BOOTING) {
		if (os->param.bp.status == 1) {
			return IHK_OS_STATUS_BOOTED;
		} else if(os->param.bp.status == 2) {
			return IHK_OS_STATUS_READY;
		} else {
			return IHK_OS_STATUS_BOOTING;
		}
	} else {
		return IHK_OS_STATUS_NOT_BOOTED;
	}
}

static int builtin_ihk_os_set_kargs(ihk_os_t ihk_os, void *priv, char *buf)
{
	unsigned long flags;
	struct builtin_os_data *os = priv;

	spin_lock_irqsave(&os->lock, flags);
	if (os->status != BUILTIN_OS_STATUS_INITIAL) {
		printk("builtin: OS status is not initial.\n");
		spin_unlock_irqrestore(&os->lock, flags);
		return -EBUSY;
	}
	os->status = BUILTIN_OS_STATUS_LOADING;
	spin_unlock_irqrestore(&os->lock, flags);

	strncpy(os->kernel_args, buf, sizeof(os->kernel_args));

	set_os_status(os, BUILTIN_OS_STATUS_INITIAL);

	return 0;
}

static int builtin_ihk_os_wait_for_status(ihk_os_t ihk_os, void *priv,
                                      enum ihk_os_status status, 
                                      int sleepable, int timeout)
{
	enum ihk_os_status s;
	if (sleepable) {
		/* TODO: Enable notification of status change, and wait */
		return -1;
	} else {
		/* Polling */
		while ((s = builtin_ihk_os_query_status(ihk_os, priv)),
		       s != status && s < IHK_OS_STATUS_SHUTDOWN 
		       && timeout > 0) {
			mdelay(100);
			timeout--;
		}
		return s == status ? 0 : -1;
	}
}

static int builtin_ihk_os_issue_interrupt(ihk_os_t ihk_os, void *priv,
                                      int cpu, int v)
{
	struct builtin_os_data *os = priv;

	/* better calcuation or make map */
	if (cpu < 0 || cpu >= os->cpu_info.n_cpus) {
		return -EINVAL;
	}
	//printk("builtin_ihk_os_issue_interrupt(): %d\n", os->cpu_info.hw_ids[cpu]);
	//shimos_issue_ipi(os->cpu_info.hw_ids[cpu], v);
	
	__default_send_IPI_dest_field(os->cpu_info.hw_ids[cpu], v, 
			APIC_DEST_PHYSICAL);

	return -EINVAL;
}

static unsigned long builtin_ihk_os_map_memory(ihk_os_t ihk_os, void *priv,
                                           unsigned long remote_phys,
                                           unsigned long size)
{
	/* We use the same physical memory. So no need to do something */
	return remote_phys;
}

static int builtin_ihk_os_unmap_memory(ihk_os_t ihk_os, void *priv,
                                    unsigned long local_phys,
                                    unsigned long size)
{
	return 0;
}

static int builtin_ihk_os_get_special_addr(ihk_os_t ihk_os, void *priv,
                                       enum ihk_special_addr_type type,
                                       unsigned long *addr,
                                       unsigned long *size)
{
	struct builtin_os_data *os = priv;

	switch (type) {
	case IHK_SPADDR_KMSG:
		if (os->param.msg_buffer) {
			*addr = os->param.msg_buffer;
			*size = 8192;
			return 0;
		}
		break;

	case IHK_SPADDR_MIKC_QUEUE_RECV:
		if (os->param.mikc_queue_recv) {
			*addr = os->param.mikc_queue_recv;
			*size = MASTER_IKCQ_SIZE;
			return 0;
		}
		break;
	case IHK_SPADDR_MIKC_QUEUE_SEND:
		if (os->param.mikc_queue_send) {
			*addr = os->param.mikc_queue_send;
			*size = MASTER_IKCQ_SIZE;
			return 0;
		}
		break;
	}

	return -EINVAL;
}

static long builtin_ihk_os_debug_request(ihk_os_t ihk_os, void *priv,
                                     unsigned int req, unsigned long arg)
{
	switch (req) {
	case IHK_OS_DEBUG_START:
		builtin_ihk_os_issue_interrupt(ihk_os, priv, (arg >> 8),
		                           (arg & 0xff));
		return 0;
	}
	return -EINVAL;
}

static LIST_HEAD(builtin_interrupt_handlers);

static int builtin_ihk_os_register_handler(ihk_os_t os, void *os_priv, int itype,
                                       struct ihk_host_interrupt_handler *h)
{
	h->os = os;
	h->os_priv = os_priv;
	list_add_tail(&h->list, &builtin_interrupt_handlers);

	return 0;
}

static int builtin_ihk_os_unregister_handler(ihk_os_t os, void *os_priv, int itype,
                                         struct ihk_host_interrupt_handler *h)
{
	list_del(&h->list);
	return 0;
}

static irqreturn_t builtin_irq_handler(int irq, void *data)
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

static struct ihk_mem_info *builtin_ihk_os_get_memory_info(ihk_os_t ihk_os,
                                                       void *priv)
{
	struct builtin_os_data *os = priv;

	return &os->mem_info;
}

static struct ihk_cpu_info *builtin_ihk_os_get_cpu_info(ihk_os_t ihk_os, void *priv)
{
	struct builtin_os_data *os = priv;

	return &os->cpu_info;
}

static struct ihk_os_ops builtin_ihk_os_ops = {
	.load_mem = builtin_ihk_os_load_mem,
	.load_file = builtin_ihk_os_load_file,
	.boot = builtin_ihk_os_boot,
	.shutdown = builtin_ihk_os_shutdown,
	.alloc_resource = builtin_ihk_os_alloc_resource,
	.query_status = builtin_ihk_os_query_status,
	.wait_for_status = builtin_ihk_os_wait_for_status,
	.set_kargs = builtin_ihk_os_set_kargs,
	.issue_interrupt = builtin_ihk_os_issue_interrupt,
	.map_memory = builtin_ihk_os_map_memory,
	.unmap_memory = builtin_ihk_os_unmap_memory,
	.register_handler = builtin_ihk_os_register_handler,
	.unregister_handler = builtin_ihk_os_unregister_handler,
	.get_special_addr = builtin_ihk_os_get_special_addr,
	.debug_request = builtin_ihk_os_debug_request,
	.get_memory_info = builtin_ihk_os_get_memory_info,
	.get_cpu_info = builtin_ihk_os_get_cpu_info,
};	

static struct ihk_register_os_data builtin_os_reg_data = {
	.name = "builtinos",
	.flag = 0,
	.ops = &builtin_ihk_os_ops,
};

static int builtin_ihk_create_os(ihk_device_t ihk_dev, void *priv,
                             unsigned long arg, ihk_os_t ihk_os,
                             struct ihk_register_os_data *regdata)
{
	struct builtin_device_data *data = priv;
	struct builtin_os_data *os;

	*regdata = builtin_os_reg_data;

	os = kzalloc(sizeof(struct builtin_os_data), GFP_KERNEL);
	if (!os) {
		data->status = 0; /* No other one should reach here */
		return -ENOMEM;
	}
	spin_lock_init(&os->lock);
	os->dev = data;
	regdata->priv = os;

	return 0;
}

/** \brief Map a remote physical memory to the local physical memory.
 *
 * In BUILTIN, all the kernels including the host kernel are running in the
 * same physical memory map, thus there is nothing to do. */
static unsigned long builtin_ihk_map_memory(ihk_device_t ihk_dev, void *priv,
                                        unsigned long remote_phys,
                                        unsigned long size)
{
	/* We use the same physical memory. So no need to do something */
	return remote_phys;
}

static int builtin_ihk_unmap_memory(ihk_device_t ihk_dev, void *priv,
                                unsigned long local_phys,
                                unsigned long size)
{
	return 0;
}



static void *builtin_ihk_map_virtual(ihk_device_t ihk_dev, void *priv,
                                 unsigned long phys, unsigned long size,
                                 void *virt, int flags)
{
	if (!virt) {
		void *ret;
		
		ret = ihk_smp_map_virtual(phys, size);
		if (!ret) {
			printk("WARNING: ihk_smp_map_virtual() returned NULL!\n");
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

static int builtin_ihk_unmap_virtual(ihk_device_t ihk_dev, void *priv,
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

static long builtin_ihk_debug_request(ihk_device_t ihk_dev, void *priv,
                                  unsigned int req, unsigned long arg)
{
	return -EINVAL;
}


static void smp_ihk_init_ident_page_table(void)
{
	int ident_npages = 0;
	int i, j, k, ident_npages_order;
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

	ident_pages = alloc_pages(GFP_DMA | GFP_KERNEL, ident_npages_order);
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
}


static irqreturn_t smp_ihk_interrupt(int irq, void *dev_id) 
{
	ack_APIC_irq();
	builtin_irq_handler(ihk_smp_irq, NULL);
	return IRQ_HANDLED;
}

#ifdef CONFIG_SPARSE_IRQ
void (*orig_irq_flow_handler)(unsigned int irq, struct irq_desc *desc) = NULL;
void ihk_smp_irq_flow_handler(unsigned int irq, struct irq_desc *desc)
{
	if (!desc->action || !desc->action->handler) {
		printk("IHK-SMP: no handler for IRQ %d??\n", irq);
		return;
	}
	
	spin_lock(&desc->lock);

	//printk("IHK-SMP: calling handler for IRQ %d\n", irq);
	desc->action->handler(irq, NULL);
	//ack_APIC_irq();
	
	spin_unlock(&desc->lock);
}
#endif

int shimos_nchunks = 16;

int ihk_smp_reserve_mem(void)
{
	const int order = 10;		/* 4 MiB a chunk */
	size_t want;
	size_t alloced;
	int nchunk;
	struct chunk *p;
	struct chunk *q;
	void *va;
	size_t remain;
	int i;
	int ret;

	INIT_LIST_HEAD(&ihk_mem_free_chunks);
	INIT_LIST_HEAD(&ihk_mem_used_chunks);
	INIT_LIST_HEAD(&unused);

	if (!ihk_mem) {
		if (!ihk_phys_start) {
			printk("IHK-SMP: error: both ihk_mem and ihk_phys_start are 0\n");
			ret = -1;
			goto out;
		}
		
		printk("IHK-SMP: ihk_phys_start is 0x%lx\n", ihk_phys_start);
		return 0;
	}
	
	/* ihk_mem is in MBs */
	ihk_mem <<= 20;

	printk(KERN_INFO "IHK-SMP: ihk_mem: %lu bytes\n", ihk_mem);

	want = ihk_mem & ~((PAGE_SIZE << order) - 1);
	alloced = 0;
	nchunk = 0;

	/* allocate and merge pages */
	while (alloced < want) {
		p = (void *)__get_free_pages(GFP_KERNEL, order);
		
		if (!p) {
			printk(KERN_ERR "IHK-SMP: __get_free_pages() failed. %ld bytes have been allocated\n", alloced);
			printk(KERN_NOTICE "IHK-SMP: ihk_mem is ignored\n");
			
			ret = -1;
			goto out;
		}
		
		alloced += PAGE_SIZE << order;

		p->addr = virt_to_phys(p);
		p->size = PAGE_SIZE << order;
		INIT_LIST_HEAD(&p->chain);

		/* insert a chunk in physical address ascending order */
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
		
		++nchunk;

		q = list_entry(p->chain.next, struct chunk, chain);
		if (((void *)q != &ihk_mem_free_chunks) && 
				((p->addr + p->size) == q->addr)) {
			list_del(&q->chain);
			p->size += q->size;
			--nchunk;
		}

		q = list_entry(p->chain.prev, struct chunk, chain);
		if (((void *)q != &ihk_mem_free_chunks) && 
				((q->addr + q->size) == p->addr)) {
			list_del(&p->chain);
			q->size += p->size;
			--nchunk;
		}
	}

	/* free excess and small chunks */
	/* XXX: There may be a performance problem when allocated pages 
	 * fragment too many */
	while (nchunk > shimos_nchunks) {
		q = NULL;
		list_for_each_entry(p, &ihk_mem_free_chunks, chain) {
			if (!q || (p->size < q->size)) {
				q = p;
			}
		}

		list_move(&q->chain, &unused);
		--nchunk;
	}

	if (1) {
		i = 0;
		list_for_each_entry(p, &ihk_mem_free_chunks, chain) {
			printk(KERN_INFO "IHK-SMP: chunk #%d: 0x%lx - 0x%lx\n",
					i, p->addr, p->addr+p->size);
			++i;
		}
	}
	
	ret = 0;

out:
	/* free unused chunks */
	//list_splice(&ihk_mem_free_chunks, &unused);
	nchunk = 0;
	list_for_each_entry_safe(p, q, &unused, chain) {
		list_del(&p->chain);

		va = phys_to_virt(p->addr);
		remain = p->size;
		
		while (remain > 0) {
			free_pages((uintptr_t)va, order);
			va += PAGE_SIZE << order;
			remain -= PAGE_SIZE << order;
		}
	}
	
	return ret;
}


static int builtin_ihk_init(ihk_device_t ihk_dev, void *priv)
{
	struct builtin_ihk_device_ops *data = priv;
	int i = 0;
	int nr_cpus = 0;
	int cpu, apicid;
	int cpus_to_offline[256];
	int num_online_cpus_target;
	int vector = IRQ15_VECTOR + 2;

	if (ihk_cores) {
		if (ihk_cores > (num_online_cpus() - 1)) {
			printk("IHK-SMP error: only %d CPUs in total are available\n", 
					num_online_cpus());
			return EINVAL;	
		}
		
		cpus_requested = ihk_cores;
	}

	trampoline_page = alloc_pages(GFP_DMA | GFP_KERNEL, 1);
	
	if (!trampoline_page || page_to_phys(trampoline_page) > 0xFF000) {
		printk("IHK-SMP error: allocating trampoline_code\n");
		return EFAULT;
	}
	printk("IHK-SMP: trampoline_page phys: 0x%llx\n", page_to_phys(trampoline_page));

	if (ihk_smp_reserve_mem() < 0) {
		printk("IHK-SMP error: reserving memory\n");
		return ENOMEM;
	}

	memset(reserved_cpu_ids, sizeof(reserved_cpu_ids), 0);

	printk("IHK-SMP: attempting to offline %d CPUs\n", cpus_requested);
	//printk("num_online_cpus: %d\n", num_online_cpus());
	num_online_cpus_target = num_online_cpus() - cpus_requested;

	for_each_online_cpu(cpu) {
		if (++nr_cpus > num_online_cpus_target) {
			cpus_to_offline[i++] = cpu;
		}
	}

	for (i = 0; i < cpus_requested; ++i) {
		int ret;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
		struct device *dev = get_cpu_device(cpus_to_offline[i]);
		//struct cpu *cpu = container_of(dev, struct cpu, dev);
#else
		struct sys_device *dev = get_cpu_sysdev(cpus_to_offline[i]);
		struct cpu *cpu = container_of(dev, struct cpu, sysdev);
#endif

		reserved_cpu_ids[i].id = cpus_to_offline[i];
		reserved_cpu_ids[i].apic_id = 
			per_cpu(x86_cpu_to_apicid, cpus_to_offline[i]);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
		ret = dev->bus->offline(dev);
		if (!ret) {
			kobject_uevent(&dev->kobj, KOBJ_OFFLINE);
			dev->offline = true;
		}
#else
		_cpu_hotplug_driver_lock();

		ret = cpu_down(cpu->sysdev.id);
		if (!ret)
			kobject_uevent(&dev->kobj, KOBJ_OFFLINE);
		
		_cpu_hotplug_driver_unlock();
#endif

		if (ret < 0) {
			printk("ERROR: hot-unplugging CPU\n");
			return EFAULT;
		}
		
		ihk_smp_reset_cpu(reserved_cpu_ids[i].apic_id);
		
		printk("CPU %d disabled successfully, APIC: %d\n", 
			reserved_cpu_ids[i].id, reserved_cpu_ids[i].apic_id);
	}
	
	/* Find a suitable IRQ vector */
	for (vector = IRQ14_VECTOR + 2; vector < 256; vector += 1) {
		struct irq_desc *desc;

		if (test_bit(vector, used_vectors)) {
			printk("IRQ vector %d: used\n", vector);
			continue;
		}
		
#ifdef CONFIG_SPARSE_IRQ
		/* If no descriptor, create one */
		desc = _irq_to_desc(vector);
		if (!desc) {
			printk("IRQ vector %d: no descriptor, allocating\n", vector);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0)
			desc = _alloc_desc(vector, first_online_node, THIS_MODULE);
			desc->irq_data.chip = _dummy_irq_chip;
			radix_tree_insert(_irq_desc_tree, vector, desc);
#else
			desc = _irq_to_desc_alloc_node(vector, first_online_node);
			if (!desc) {
				printk("IRQ vector %d: still no descriptor??\n", vector);	
				continue;
			}
			desc->chip = _dummy_irq_chip;
#endif
		}
		
		desc = _irq_to_desc(vector);
		if (!desc) {
			printk("IRQ vector %d: still no descriptor??\n", vector);	
			continue;
		}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0)
		if (desc->status_use_accessors & IRQ_NOREQUEST) {
			
			printk("IRQ vector %d: not allowed to request, fake it\n", vector);
			
			desc->status_use_accessors &= ~IRQ_NOREQUEST;
		}
		
		desc->handle_irq = ihk_smp_irq_flow_handler;
#else
		if (desc->status & IRQ_NOREQUEST) {
			
			printk("IRQ vector %d: not allowed to request, fake it\n", vector);
			
			desc->status &= ~IRQ_NOREQUEST;
		}
		
		desc->handle_irq = ihk_smp_irq_flow_handler;
#endif
#endif // CONFIG_SPARSE_IRQ

		if (request_irq(vector, 
					smp_ihk_interrupt, IRQF_DISABLED, "IHK-SMP", NULL) != 0) { 
			printk("IRQ vector %d: request_irq failed\n", vector);
			continue;
		}
	
		/* Pretend a real external interrupt */
		{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
			int *vectors = (*SHIFT_PERCPU_PTR((vector_irq_t *)_vector_irq, 
						per_cpu_offset(smp_processor_id())));
#else
			int *vectors = 
				(*SHIFT_PERCPU_PTR((vector_irq_t *)_per_cpu__vector_irq, 
				per_cpu_offset(smp_processor_id())));
#endif			
		
			if (vectors[vector] == -1) {
				printk("fixed vector_irq for %d\n", vector);
				vectors[vector] = vector;
			}
			
		}
		
		break;
	}

	if (vector >= 256) {
		printk("error: allocating IKC irq vector\n");
		return EFAULT;
	}

	ihk_smp_irq = vector;
	ihk_smp_irq_apicid = (int)per_cpu(x86_bios_cpu_apicid, smp_processor_id());
	printk("IHK-SMP: IKC irq vector: %d, CPU APIC id: %d\n", 
		ihk_smp_irq, ihk_smp_irq_apicid);

	smp_ihk_init_ident_page_table();

	return 0;
}

int ihk_smp_reset_cpu(int phys_apicid) {
	unsigned long send_status;
	int maxlvt;

	printk("IHK-SMP: resetting CPU %d.\n", phys_apicid);

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

	return 0;
}

static int builtin_ihk_exit(ihk_device_t ihk_dev, void *priv) 
{
	int i;

#if 0
	for (i = 0; i < cpus_requested; ++i) {
		int ret;
		struct sys_device *dev = get_cpu_sysdev(reserved_cpu_ids[i].id);
		struct cpu *cpu = container_of(dev, struct cpu, sysdev);

		ihk_smp_reset_cpu(reserved_cpu_ids[i].apic_id);

		cpu_hotplug_driver_lock();

		ret = _cpu_up(cpu->sysdev.id);
		if (!ret)
			kobject_uevent(&dev->kobj, KOBJ_ONLINE);
		
		cpu_hotplug_driver_unlock();
		
		if (ret < 0) {
			printk("ERROR: hot-plugging CPU (skipping)\n");
			continue;
		}
		
		printk("CPU %d re-enabled successfully, APIC: %d\n", 
			reserved_cpu_ids[i].id, reserved_cpu_ids[i].apic_id);
	}
#endif 

	struct irq_desc *desc;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
	int *vectors = (*SHIFT_PERCPU_PTR((vector_irq_t *)_vector_irq, 
				per_cpu_offset(smp_processor_id())));
#else
	int *vectors = (*SHIFT_PERCPU_PTR((vector_irq_t *)_per_cpu__vector_irq, 
				per_cpu_offset(smp_processor_id())));
#endif	

	vectors[ihk_smp_irq] == -1;

	//desc = _irq_to_desc(ihk_smp_irq);
	//desc->handle_irq = orig_irq_flow_handler;
	
	free_irq(ihk_smp_irq, NULL);

	if (trampoline_page) {
		free_pages((unsigned long)pfn_to_kaddr(page_to_pfn(trampoline_page)), 1);
	}

	if (ident_npages_order) {
		free_pages((unsigned long)ident_page_table_virt, ident_npages_order);
	}

	return 0;
}

static struct ihk_device_ops builtin_ihk_device_ops = {
	.init = builtin_ihk_init,
	.exit = builtin_ihk_exit,
	.create_os = builtin_ihk_create_os,
	.map_memory = builtin_ihk_map_memory,
	.unmap_memory = builtin_ihk_unmap_memory,
	.map_virtual = builtin_ihk_map_virtual,
	.unmap_virtual = builtin_ihk_unmap_virtual,
	.debug_request = builtin_ihk_debug_request,
	.get_dma_channel = builtin_ihk_get_dma_channel,
};	

/** \brief The driver-specific driver structure
 *
 * Since there is only one BUILTIN "device" in machine, this structure is
 * statically allocated. */
static struct builtin_device_data builtin_data;

static struct ihk_register_device_data builtin_dev_reg_data = {
	.name = "builtin",
	.flag = 0,
	.priv = &builtin_data,
	.ops = &builtin_ihk_device_ops,
};

static int __init builtin_init(void)
{
	ihk_device_t ihkd;

	printk(KERN_INFO "IHK-SMP: initializing...\n");

	spin_lock_init(&builtin_data.lock);

	if (!(ihkd = ihk_register_device(&builtin_dev_reg_data))) {
		printk(KERN_INFO "builtin: Failed to register ihk driver.\n");
		return -ENOMEM;
	}

	builtin_data.ihk_dev = ihkd;

	//shimos_set_irq_handler(builtin_irq_handler);

	return 0;
}

static void __exit builtin_exit(void)
{
	printk(KERN_INFO "IHK-SMP: finalizing...\n");
	ihk_unregister_device(builtin_data.ihk_dev);

	//shimos_set_irq_handler(NULL);
}

module_init(builtin_init);
module_exit(builtin_exit);

MODULE_LICENSE("Dual BSD/GPL");
