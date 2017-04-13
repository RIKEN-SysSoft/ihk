/* smp-driver.c COPYRIGHT FUJITSU LIMITED 2015-2016 */
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
#include <linux/delay.h>
#include <linux/elf.h>
#include <linux/file.h>
#include <linux/pci.h>
#include <linux/version.h>
#include <linux/cpu.h>
#ifdef POSTK_DEBUG_ARCH_DEP_57 /* add ctype.h include for isspace() / isdigit() */
#include <linux/ctype.h>
#endif /* POSTK_DEBUG_ARCH_DEP_57 */
#if LINUX_VERSION_CODE == KERNEL_VERSION(2,6,32)
#include <linux/autoconf.h>
#endif
#include <asm/uaccess.h>
#include <ihk/ihk_host_driver.h>
#include <ihk/ihk_host_misc.h>
#include <ihk/ihk_host_user.h>
//#define IHK_DEBUG
#include <ihk/misc/debug.h>
#include <ikc/msg.h>
//#include <linux/shimos.h>
//#include "builtin_dma.h"
#include <bootparam.h>
#include "config.h"
#include "smp-driver.h"
#include "smp-arch-driver.h"
#include "smp-defines-driver.h"

/*
 * IHK-SMP unexported kernel symbols
 */

/* ----------------------------------------------- */

static unsigned long ihk_phys_start = 0;
module_param(ihk_phys_start, ulong, 0644);
MODULE_PARM_DESC(ihk_phys_start, "IHK reserved physical memory start address");

static unsigned long ihk_mem = 0;
module_param(ihk_mem, ulong, 0644);
MODULE_PARM_DESC(ihk_mem, "IHK reserved memory in MBs");

static unsigned int ihk_cores = 0;
module_param(ihk_cores, uint, 0644);
MODULE_PARM_DESC(ihk_cores, "IHK reserved CPU cores");

//#define BUILTIN_COM_VECTOR	0xf1

#if defined(RHEL_RELEASE_CODE) || (LINUX_VERSION_CODE < KERNEL_VERSION(4,0,0))
#define BITMAP_SCNLISTPRINTF(buf, buflen, maskp, nmaskbits) \
        bitmap_scnlistprintf(buf, buflen, maskp, nmaskbits)
#else
#define BITMAP_SCNLISTPRINTF(buf, buflen, maskp, nmaskbits) \
        scnprintf(buf, buflen, "%*pbl", nmaskbits, maskp)
#endif

#define BUILTIN_DEV_STATUS_READY	0
#define BUILTIN_DEV_STATUS_BOOTING	1

struct ihk_smp_cpu ihk_smp_cpus[SMP_MAX_CPUS];
unsigned long trampoline_phys;

unsigned long ident_page_table;

int this_module_put = 0;

static struct list_head ihk_mem_free_chunks;
struct list_head ihk_mem_used_chunks;

/* ----------------------------------------------- */

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
	int numa_id;
};

/* ----------------------------------------------- */

void *ihk_smp_map_virtual(unsigned long phys, unsigned long size)
{
	struct ihk_os_mem_chunk *os_mem_chunk = NULL;

	/* look up address among used chunks */
	list_for_each_entry(os_mem_chunk, &ihk_mem_used_chunks, list) {
		if (phys >= os_mem_chunk->addr &&
		    (phys + size) <= (os_mem_chunk->addr + os_mem_chunk->size)) {
			return (phys_to_virt(os_mem_chunk->addr) +
			        (phys - os_mem_chunk->addr));
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

	param_pages = alloc_pages(GFP_KERNEL, param_pages_order);
	if (!param_pages) {
		kfree(os);
		printk("IHK-SMP: error: allocating boot parameter structure\n");
		return -ENOMEM;
	}

	os->param = pfn_to_kaddr(page_to_pfn(param_pages));
	dprintf("IHK-SMP: param size: %lu, nr_pages: %lu\n",
		sizeof(*os->param), 1UL << param_pages_order);

	memset(os->param, 0, param_size);
	os->param->nr_cpus = os->nr_cpus;
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

		dprintf("IHK-SMP: OS: %p, Linux NUMA: %d, CPU HWID: %d\n",
				os, cpu_to_node(cpu), ihk_smp_cpus[cpu].hw_id);

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

#ifdef POSTK_DEBUG_ARCH_DEP_29
	os->param->ns_per_tsc = calc_ns_per_tsc();
#else	/* POSTK_DEBUG_ARCH_DEP_29 */
	os->param->ns_per_tsc = 1000000000L / tsc_khz;
#endif	/* POSTK_DEBUG_ARCH_DEP_29 */
	getnstimeofday(&now);
	os->param->boot_sec = now.tv_sec;
	os->param->boot_nsec = now.tv_nsec;

	dprintf("boot cpu : %d, %lx, %lx, %lx, %lx\n",
	        os->boot_cpu, os->mem_start, os->mem_end, os->cpu_hw_ids_map.set[0],
	        os->param->dma_address
	);

	smp_ihk_setup_trampoline(os);

	printk("IHK-SMP: booting OS 0x%lx, calling wakeup_secondary_cpu() \n",
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
	phys = (os->bootstrap_mem_start + IHK_SMP_LARGE_PAGE * 2 - 1) & IHK_SMP_LARGE_PAGE_MASK;
	maxoffset = phys;

	entry = smp_ihk_adjust_entry(entry, phys);

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

		offset = elf64p[i].p_vaddr - (IHK_SMP_MAP_KERNEL_START -phys);
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

	smp_ihk_os_setup_startup(os, phys, entry);

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
	        chunk->addr, chunk->addr + chunk->size);
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

			mem_chunk->size = mem_chunk->size +
			                  mem_chunk_next->size;
			list_del(&mem_chunk_next->chain);

			goto rerun;
		}
	}
}

static size_t max_size_mem_chunk(struct list_head *chunks)
{
	size_t max = 0;
	struct chunk *chunk_iter;

	list_for_each_entry(chunk_iter, chunks, chain) {
		if (chunk_iter->size > max) {
			max = chunk_iter->size;
		}
	}

	return max;
}

static int smp_ihk_os_shutdown(ihk_os_t ihk_os, void *priv, int flag)
{
	struct smp_os_data *os = priv;
	int i, ret = 0;
	struct ihk_os_mem_chunk *os_mem_chunk = NULL;
	struct ihk_os_mem_chunk *next_chunk = NULL;
	struct chunk *mem_chunk;

	/* Reset CPU cores used by this OS */
	for (i = 0; i < SMP_MAX_CPUS; ++i) {
		if (ihk_smp_cpus[i].os != ihk_os)
			continue;

		ret = ihk_smp_reset_cpu(ihk_smp_cpus[i].hw_id);
		ihk_smp_cpus[i].status = IHK_SMP_CPU_AVAILABLE;
		ihk_smp_cpus[i].os = (ihk_os_t)0;

		dprintk("IHK-SMP: CPU %d has been deassigned, HWID: %d\n",
		       ihk_smp_cpus[i].id, ihk_smp_cpus[i].hw_id);
	}

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

	set_os_status(os, BUILTIN_OS_STATUS_INITIAL);
	if (os->numa_mapping) {
		kfree(os->numa_mapping);
		os->numa_mapping = NULL;
	}

	return ret;
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

			printk("IHK-SMP: CPU HWID %d assigned.\n",
			       ihk_smp_cpus[i].hw_id);
			CORE_SET(ihk_smp_cpus[i].hw_id, os->cpu_hw_ids_map);

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
		os_mem_chunk = kmalloc(sizeof(struct ihk_os_mem_chunk),
		                       GFP_KERNEL);

		if (!os_mem_chunk) {
			printk("IHK-DMP: error: allocating os_mem_chunk\n");
			return -ENOMEM;
		}

		os_mem_chunk->addr = 0;
		INIT_LIST_HEAD(&os_mem_chunk->list);

		list_for_each_entry(mem_chunk_iter, &ihk_mem_free_chunks,
		                    chain) {
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
			mem_chunk_leftover =
			    (struct chunk*)
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

		printk("IHK-SMP: CPU HWID %d deassigned.\n",
		       ihk_smp_cpus[i].hw_id);

		ihk_smp_cpus[i].status = IHK_SMP_CPU_AVAILABLE;
		ihk_smp_cpus[i].os = (ihk_os_t)0;
	}

	return ret;
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

	set_os_status(os, BUILTIN_OS_STATUS_INITIAL);

	return 0;
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

static int smp_ihk_os_get_special_addr(ihk_os_t ihk_os, void *priv,
                                       enum ihk_special_addr_type type,
                                       unsigned long *addr,
                                       unsigned long *size)
{
	struct smp_os_data *os = priv;

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

irqreturn_t smp_ihk_irq_call_handlers(int irq, void *data)
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

			CORE_SET(ihk_smp_cpus[cpu].hw_id, os->cpu_hw_ids_map);
			set_bit(cpu_to_node(cpu), &os->numa_mask);

			ihk_smp_cpus[cpu].status = IHK_SMP_CPU_ASSIGNED;
			ihk_smp_cpus[cpu].os = ihk_os;

			os->cpu_mapping[os->nr_cpus] = cpu;
			os->cpu_hw_ids[os->nr_cpus] = ihk_smp_cpus[cpu].hw_id;
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
	int ret = 0;
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

		ret = ihk_smp_reset_cpu(ihk_smp_cpus[cpu].hw_id);
		CORE_CLR(ihk_smp_cpus[cpu].hw_id, os->cpu_hw_ids_map);

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

		dprintk(KERN_INFO "IHK-SMP: CPU HWID %d released from %p\n",
				ihk_smp_cpus[cpu].hw_id, ihk_os);
	}

	printk(KERN_INFO "IHK-SMP: released CPUs: %s from OS %p\n",
		(const char __user *)arg, ihk_os);

out:
	if (req_string) kfree(req_string);
	return ret;
}

char query_res[8192];

static int smp_ihk_os_query_cpu(ihk_os_t ihk_os, void *priv, unsigned long arg)
{
	int cpu;
	cpumask_t cpus_assigned;

	memset(&cpus_assigned, 0, sizeof(cpus_assigned));
	memset(query_res, 0, sizeof(query_res));

	for (cpu = 0; cpu < SMP_MAX_CPUS; ++cpu) {
		if (ihk_smp_cpus[cpu].status != IHK_SMP_CPU_ASSIGNED)
			continue;
		if (ihk_smp_cpus[cpu].os != ihk_os)
			continue;

		cpumask_set_cpu(cpu, &cpus_assigned);
	}

	BITMAP_SCNLISTPRINTF(query_res, sizeof(query_res),
		cpumask_bits(&cpus_assigned), nr_cpumask_bits);

	if (strlen(query_res) > 0) {
		if (copy_to_user((char *)arg, query_res, strlen(query_res) + 1)) {
			printk("IHK-SMP: error: copying CPU string to user-space\n");
			return -EINVAL;
		}
	}

	return 0;
}

static int smp_ihk_os_ikc_map(ihk_os_t ihk_os, void *priv, unsigned long arg)
{
	int ret = 0;
	struct smp_os_data *os = priv;
	cpumask_t cpus_to_map;
	unsigned long flags;
	char *string = (char *)arg;
	char *token;

	spin_lock_irqsave(&os->lock, flags);
	if (os->status != BUILTIN_OS_STATUS_INITIAL) {
		spin_unlock_irqrestore(&os->lock, flags);
		ret = -EBUSY;
		goto out;
	}
	spin_unlock_irqrestore(&os->lock, flags);

	token = strsep(&string, "+");
	while (token) {

		memset(&cpus_to_map, 0, sizeof(cpus_to_map));
		printk("%s: %s\n", __FUNCTION__, token);

		token = strsep(&string, "+");
	}

out:
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
				mem_chunk_leftover = (struct chunk*)
					phys_to_virt(mem_chunk_max->addr + mem_size);
				mem_chunk_leftover->addr = mem_chunk_max->addr + mem_size;
				mem_chunk_leftover->size = mem_chunk_max->size - mem_size;
				mem_chunk_leftover->numa_id = mem_chunk_max->numa_id;

				add_free_mem_chunk(mem_chunk_leftover);
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

		printk(KERN_INFO "IHK-SMP: memory 0x%lx - 0x%lx (len: %lu) @ NUMA node %d assigned to %p\n",
				os_mem_chunk->addr, os_mem_chunk->addr + os_mem_chunk->size, os_mem_chunk->size,
				numa_id, ihk_os);
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
	struct ihk_os_mem_chunk *os_mem_chunk = NULL;
	struct ihk_os_mem_chunk *next_chunk = NULL;
	struct chunk *mem_chunk;

	spin_lock_irqsave(&os->lock, flags);
	if (os->status != BUILTIN_OS_STATUS_INITIAL) {
		spin_unlock_irqrestore(&os->lock, flags);
		return -EBUSY;
	}
	spin_unlock_irqrestore(&os->lock, flags);

	/* Drop memory chunk used by this OS */
	/* TODO: parse user string */
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

	return 0;
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
	.query_cpu = smp_ihk_os_query_cpu,
	.assign_mem = smp_ihk_os_assign_mem,
	.release_mem = smp_ihk_os_release_mem,
	.query_mem = smp_ihk_os_query_mem,
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
		return shimos_other_os_unmap(virt, size);
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
			/* NOTE: memory was allocated via __get_free_pages() in 4MB blocks */
			int order = 10;
			int order_size = 4194304;

			free_pages(va, order);
			pr_debug("0x%lx, page order: %d freed\n", va, order);
			size_left -= order_size;
			va += order_size;
		}

		dprintf("IHK-SMP: 0x%lx - 0x%lx freed\n", pa, pa + size);
	}

	return 0;
}

static int __ihk_smp_reserve_mem(size_t ihk_mem, int numa_id)
{
	const int order = get_order(IHK_SMP_CHUNK_BASE_SIZE);
	size_t want;
	size_t allocated;
	struct chunk *p;
	struct chunk *q;
	int ret = 0;
	struct list_head tmp_chunks;
	nodemask_t nodemask;

	memset(&nodemask, 0, sizeof(nodemask));
	__node_set(numa_id, &nodemask);

	INIT_LIST_HEAD(&tmp_chunks);

	dprintk(KERN_INFO "IHK-SMP: __ihk_smp_reserve_mem: %lu bytes\n", ihk_mem);

	want = ihk_mem & ~((PAGE_SIZE << order) - 1);
	allocated = 0;

	/* Allocate and merge pages until we get a contigous area
	 * or run out of free memory. Keep the longest areas */
	while (max_size_mem_chunk(&tmp_chunks) < want) {
		struct page *pg;

		pg = __alloc_pages_nodemask(
				GFP_KERNEL | __GFP_COMP | __GFP_NOWARN,
				order,
				node_zonelist(numa_id, GFP_KERNEL | __GFP_COMP), &nodemask);
		if (!pg) {
			/*
			 * We ran out of memory before finding a single contigous
			 * chunk, but do we have enough in multiple chunks?
			 */
			if (allocated >= want) break;

			printk(KERN_ERR "IHK-SMP: error: __alloc_pages_node() failed\n");

			ret = -1;
			goto out;
		}

		p = page_address(pg);

		allocated += PAGE_SIZE << order;

		p->addr = virt_to_phys(p);
		p->size = PAGE_SIZE << order;
		p->numa_id = numa_id;
		INIT_LIST_HEAD(&p->chain);

		/* Insert the chunk in physical address ascending order */
		list_for_each_entry(q, &tmp_chunks, chain) {
			if (p->addr < q->addr) {
				break;
			}
		}

		if ((void *)q == &tmp_chunks) {
			list_add_tail(&p->chain, &tmp_chunks);
		}
		else {
			list_add_tail(&p->chain, &q->chain);
		}

		/* Merge adjucent chunks */
		merge_mem_chunks(&tmp_chunks);
	}

	/* Move the largest chunks to free list until we meet the required size */
	allocated = 0;
	while (allocated < want) {
		size_t max = 0;
		p = NULL;

		list_for_each_entry(q, &tmp_chunks, chain) {
			if (q->size > max) {
				p = q;
				max = p->size;
			}
		}

		if (!p) break;

		list_del(&p->chain);

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
	__smp_ihk_free_mem_from_list(&tmp_chunks);

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
			printk("IHK-SMP: error: CPU %d is out of limit\n",
			       cpu);
			ret = -EINVAL;
			goto err_before_offline;
		}

		if (!cpu_present(cpu)) {
			printk("IHK-SMP: error: CPU %d is not present\n",
			       cpu);
			ret = -EINVAL;
			goto err_before_offline;
		}

		if (!cpu_online(cpu)) {
			if (ihk_smp_cpus[cpu].status == IHK_SMP_CPU_AVAILABLE)
				printk("IHK-SMP: error: CPU %d was reserved already\n",
				       cpu);

			if (ihk_smp_cpus[cpu].status == IHK_SMP_CPU_ASSIGNED)
				printk("IHK-SMP: erro: CPU %d was assigned already\n",
				       cpu);

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
		ihk_smp_cpus[cpu].hw_id = ihk_smp_get_hw_id(cpu);
		ihk_smp_cpus[cpu].status = IHK_SMP_CPU_TO_OFFLINE;
		ihk_smp_cpus[cpu].os = (ihk_os_t)0;

		dprintk(KERN_INFO "IHK-SMP: CPU %d to be offlined, HWID: %d\n",
		       ihk_smp_cpus[cpu].id, ihk_smp_cpus[cpu].hw_id);
	}

	/* Offline CPU cores */
	for (cpu = 0; cpu < SMP_MAX_CPUS; ++cpu) {
		if (ihk_smp_cpus[cpu].status != IHK_SMP_CPU_TO_OFFLINE)
			continue;

		if ((ret = smp_ihk_offline_cpu(cpu)) != 0) {
			goto err_during_offline;
		}

		ihk_smp_cpus[cpu].hw_id = ihk_smp_get_hw_id(cpu);
		ihk_smp_cpus[cpu].status = IHK_SMP_CPU_OFFLINED;
		ihk_smp_cpus[cpu].os = (ihk_os_t)0;
		
		ret = ihk_smp_reset_cpu(ihk_smp_cpus[cpu].hw_id);

		dprintk(KERN_INFO "IHK-SMP: CPU %d offlined successfully, HWID: %d\n",
		       ihk_smp_cpus[cpu].id, ihk_smp_cpus[cpu].hw_id);
	}

	/* Offlining CPU cores went well, mark them as available */
	for (cpu = 0; cpu < SMP_MAX_CPUS; ++cpu) {
		if (ihk_smp_cpus[cpu].status != IHK_SMP_CPU_OFFLINED)
			continue;
		ihk_smp_cpus[cpu].status = IHK_SMP_CPU_AVAILABLE;

		dprintk(KERN_INFO "IHK-SMP: CPU %d reserved successfully, HWID: %d\n",
		       ihk_smp_cpus[cpu].id, ihk_smp_cpus[cpu].hw_id);
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
			printk("IHK-SMP: error: CPU %d is out of limit\n",
			       cpu);
			ret = -EINVAL;
			goto err;
		}

		if (!cpu_present(cpu)) {
			printk("IHK-SMP: error: CPU %d is not valid\n",
			       cpu);
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
		ihk_smp_cpus[cpu].hw_id = ihk_smp_get_hw_id(cpu);
		ihk_smp_cpus[cpu].status = IHK_SMP_CPU_TO_ONLINE;
		ihk_smp_cpus[cpu].os = (ihk_os_t)0;

		dprintk("IHK-SMP: CPU %d to be onlined, HWID: %d\n",
		       ihk_smp_cpus[cpu].id, ihk_smp_cpus[cpu].hw_id);
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

		dprintk("IHK-SMP: CPU %d onlined successfully, HWID: %d\n",
		       ihk_smp_cpus[cpu].id, ihk_smp_cpus[cpu].hw_id);
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
	int ret;

	/* Release everything for now */
	/* TODO: parse input string */
	ret = __smp_ihk_free_mem_from_list(&ihk_mem_free_chunks);

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

int read_file(void *buf, size_t size, char *fmt, va_list ap)
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

int file_readable(char *fmt, ...)
{
	int error;
	va_list ap;
	int n;
	char *filename = NULL;
	struct file *fp = NULL;

	filename = kmalloc(PATH_MAX, GFP_KERNEL);
	if (!filename) {
		error = -ENOMEM;
		eprintk("%s: kmalloc failed. %d\n",
				__FUNCTION__, error);
		return 0;
	}

	va_start(ap, fmt);
	n = vsnprintf(filename, PATH_MAX, fmt, ap);
	va_end(ap);

	if (n >= PATH_MAX) {
		error = -ENAMETOOLONG;
		eprintk("%s: vsnprintf failed. %d\n",
				__FUNCTION__, error);
		return 0;
	}

	fp = filp_open(filename, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		return 0;
	}

	error = filp_close(fp, NULL);
	return 1;
}

int read_long(long *valuep, char *fmt, ...)
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

int read_bitmap(void *map, int nbits, char *fmt, ...)
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

int read_string(char **valuep, char *fmt, ...)
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

static int smp_ihk_init(ihk_device_t ihk_dev, void *priv)
{
	int ret;

	INIT_LIST_HEAD(&ihk_mem_free_chunks);
	INIT_LIST_HEAD(&ihk_mem_used_chunks);

	if (ihk_cores) {
		if (ihk_cores > (num_present_cpus() - 1)) {
			printk("IHK-SMP error: only %d CPUs in total are available\n",
			       num_present_cpus());
			return EINVAL;
		}
	}

	memset(ihk_smp_cpus, 0, sizeof(ihk_smp_cpus));

	ret = smp_ihk_arch_init();

	return ret;
}

static int smp_ihk_exit(ihk_device_t ihk_dev, void *priv)
{
	struct chunk *mem_chunk;
	struct chunk *mem_chunk_next;
	unsigned long size_left;
	unsigned long va;
	const int order = get_order(IHK_SMP_CHUNK_BASE_SIZE);
	int cpu, ret = 0;

	smp_ihk_arch_exit();

	/* Re-enable CPU cores */
	for (cpu = 0; cpu < SMP_MAX_CPUS; ++cpu) {
		if (ihk_smp_cpus[cpu].status == IHK_SMP_CPU_ONLINE)
			continue;

		ret = ihk_smp_reset_cpu(ihk_smp_cpus[cpu].hw_id);

		if (smp_ihk_online_cpu(cpu) != 0) {
			continue;
		}

		printk("IHK-SMP: CPU %d onlined successfully, HWID: %d\n",
		       ihk_smp_cpus[cpu].id, ihk_smp_cpus[cpu].hw_id);
	}

	/* Free memory */
	list_for_each_entry_safe(mem_chunk, mem_chunk_next,
	                         &ihk_mem_free_chunks, chain) {

		list_del(&mem_chunk->chain);

		va = (unsigned long)phys_to_virt(mem_chunk->addr);
		size_left = mem_chunk->size;
		while (size_left > 0) {
			/* NOTE: memory was allocated via __get_free_pages() in 4MB blocks */
			free_pages(va, order);
			pr_debug("0x%lx, page order: %d freed\n",
			         va, order);
			size_left -= IHK_SMP_CHUNK_BASE_SIZE;
			va += IHK_SMP_CHUNK_BASE_SIZE;
		}
	}
	free_info();

	return ret;
}

static struct ihk_device_ops smp_ihk_device_ops = {
	.init = smp_ihk_init,
	.exit = smp_ihk_exit,
	.create_os = smp_ihk_create_os,
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
