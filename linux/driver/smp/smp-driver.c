/* smp-driver.c COPYRIGHT FUJITSU LIMITED 2015-2019 */
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
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/elf.h>
#include <linux/file.h>
#include <linux/pci.h>
#include <linux/version.h>
#include <linux/cpu.h>
#include <linux/rbtree.h>
#include <linux/ctype.h>
#include <linux/slub_def.h>
#include <linux/kallsyms.h>
#include <linux/list_sort.h>
#include <linux/swap.h>
#include <linux/slub_def.h>
#include <linux/time.h>
#include <linux/hugetlb.h>
#include <asm/hw_irq.h>
#include <asm/pgtable.h>
#if LINUX_VERSION_CODE == KERNEL_VERSION(2,6,32)
#include <linux/autoconf.h>
#endif
#include <asm/uaccess.h>
#include "config.h"
#include <ihk/ihk_host_driver.h>
#include <ihk/ihk_host_misc.h>
#include <ihk/ihk_host_user.h>
//#define IHK_DEBUG
#include <ihk/misc/debug.h>
#include <ikc/msg.h>
//#include <linux/shimos.h>
//#include "builtin_dma.h"
#include <host_linux.h>
#include <bootparam.h>
#ifdef ENABLE_PERF
#include <perf_event.h>
#endif
#include "smp-driver.h"
#include "smp-arch-driver.h"
#include "smp-defines-driver.h"

/** Get the index in the map array */
#define MAP_INDEX(n)    ((n) >> 6)
/** Get the bit number in a map element */
#define MAP_BIT(n)      ((n) & 0x3f)

#define IHK_SMP_MEM_ALL	(-1UL)

#define REQ_STR_MAXLEN 1024

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

#define BUILTIN_DEV_STATUS_READY	0
#define BUILTIN_DEV_STATUS_BOOTING	1

struct ihk_smp_cpu ihk_smp_cpus[SMP_MAX_CPUS];
unsigned long trampoline_phys;

unsigned long ident_page_table;

static struct list_head ihk_mem_free_chunks;
struct list_head ihk_mem_used_chunks;

static struct vmap_area *lwk_va;
static int (*ihk_ioremap_page_range)(unsigned long addr, unsigned long end,
				     phys_addr_t phys_addr, pgprot_t prot);
spinlock_t *ihk_vmap_area_lock;
struct rb_root *ihk_vmap_area_root;
static void (*ihk___insert_vmap_area)(struct vmap_area *va);
static void (*ihk___free_vmap_area)(struct vmap_area *va);

static int smp_ihk_os_get_special_addr(ihk_os_t ihk_os, void *priv,
                                       enum ihk_special_addr_type type,
                                       unsigned long *addr,
                                       unsigned long *size);
extern unsigned long smp_ihk_os_map_memory(ihk_os_t ihk_os, void *priv,
                                           unsigned long remote_phys,
                                           unsigned long size);
static void *smp_ihk_map_virtual(ihk_device_t ihk_dev, void *priv,
                                 unsigned long phys, unsigned long size,
                                 void *virt, int flags);
static int smp_ihk_unmap_virtual(ihk_device_t ihk_dev, void *priv,
                                  void *virt, unsigned long size);
extern int smp_ihk_unmap_memory(ihk_device_t ihk_dev, void *priv,
                                unsigned long local_phys,
                                unsigned long size);

extern int smp_ihk_os_send_nmi(ihk_os_t ihk_os, void *priv, int mode);
struct hstate *smp_ihk_hstates;
unsigned int *smp_ihk_default_hstate_idx;

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
	struct rb_node node;
	uintptr_t addr;
	size_t size;
	int numa_id;
};

/* ----------------------------------------------- */
static unsigned long dump_page_set_addr;
static unsigned long dump_bootstrap_mem_start;

static int truncate_snprintf(char *str, size_t size,
		const char *format, ...)
{
	int n;
	va_list ap;

	if (size <= 1) {
		return size;
	}

	va_start(ap, format);
	n = vsnprintf(str, size, format, ap);
	va_end(ap);

	if (n >= size) {
		n = size - 1;
	}

	return n;
}

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
	smp_ihk_arch_dcache_flush(virt, PAGE_SIZE);
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

static int cpu_array2str(char *str, ssize_t len,
		int num_cpus, int *cpus)
{
	/* prev_cpu should be < -1 so that "if (prev_cpu != cpus[i] - 1)"
	 * won't misunderstand that the cursor is pointing to "0"
	 * following "-1".
	 */
	int i, prev_cpu = -10, in_seq = 0;
	int n = 0;

	memset(str, 0, len);

	for (i = 0; i < num_cpus; i++) {
		if (prev_cpu != cpus[i] - 1) {
			if (prev_cpu > 0) {
				n += truncate_snprintf(str + n, len - n,
					"%d,", prev_cpu);
			}
			in_seq = 0;
		}
		else {
			if (!in_seq) {
				n += truncate_snprintf(str + n, len - n,
					"%d-", prev_cpu);
				in_seq = 1;
			}
		}

		prev_cpu = cpus[i];
	}

	if (prev_cpu >= 0) {
		n += truncate_snprintf(str + n, len - n, "%d", prev_cpu);
	}

	return n;
}

static int ikc_array2str(char *str, ssize_t len, int num_cpus,
			 int *src_cpus, int *dst_cpus)
{
	int ret = 0, max_dst = 0, n = 0;
	int *num_ikc_src = NULL;
	int src_cnt, dst, i;

	num_ikc_src = kzalloc(sizeof(int) * SMP_MAX_CPUS, GFP_KERNEL);
	if (!num_ikc_src) {
		ret = -ENOMEM;
		goto out;
	}

	for (i = 0; i < num_cpus; i++) {
		if (dst_cpus[i] < 0 || dst_cpus[i] >= SMP_MAX_CPUS) {
			pr_err("%s: error: dst cpu %d is out of range\n",
				__func__, dst_cpus[i]);
			ret = -EINVAL;
			goto out;
		}

		num_ikc_src[dst_cpus[i]]++;

		if (dst_cpus[i] > max_dst) {
			max_dst = dst_cpus[i];
		}
	}

	for (dst = 0; dst <= max_dst; dst++) {
		if (num_ikc_src[dst] == 0) {
			continue;
		}

		src_cnt = 0;
		for (i = 0; i < num_cpus; i++) {
			if (dst_cpus[i] != dst) {
				continue;
			}

			n += truncate_snprintf(str + n, len - n,
				"%d", src_cpus[i]);
			if (src_cnt != num_ikc_src[dst] - 1) {
				n += snprintf(str + n, len - n, ",");
			}
			src_cnt++;
		}

		n += truncate_snprintf(str + n, len - n, ":%d", dst);

		if (dst != max_dst) {
			n += truncate_snprintf(str + n, len - n, "+");
		}
	}
 out:
	kfree(num_ikc_src);
	return ret;
}

/* Compatibility for rdtsc()/rdtscll(). see arch/x86/include/asm/msr.h */
#if (!defined(RHEL_RELEASE_CODE) && LINUX_VERSION_CODE < KERNEL_VERSION(4, 3, 0)) || \
	(defined(RHEL_RELEASE_CODE) && RHEL_RELEASE_CODE < RHEL_RELEASE_VERSION(7, 3))
#define rdtsc __native_read_tsc
#endif

/** \brief Boot a kernel. */
static int smp_ihk_os_boot(ihk_os_t ihk_os, void *priv, int flag)
{
	struct ihk_host_linux_os_data *ihk_core_os = (struct ihk_host_linux_os_data *)ihk_os;
	struct smp_os_data *os = priv;
	struct builtin_device_data *dev = os->dev;
	unsigned long flags;
	struct timespec now;
	int param_size, param_pages_order = 0;
	struct page *param_pages;
	int dump_size, dump_pages_order = 0;
	struct page *dump_pages;
	struct ihk_os_mem_chunk *os_mem_chunk;
	int nr_memory_chunks = 0;
	int numa_id, linux_numa_id, nr_numa_nodes;
	struct ihk_smp_boot_param_cpu *bp_cpu;
	struct ihk_smp_boot_param_numa_node *bp_numa_node;
	struct ihk_smp_boot_param_memory_chunk *bp_mem_chunk;
	int lwk_cpu;
	int *ihk_smp_boot_numa_distance;
	int i, j;
	unsigned long buffer_size, map_end, index;
	struct ihk_dump_page *dump_page;
	int ret;

	spin_lock_irqsave(&dev->lock, flags);
	if (dev->status != BUILTIN_DEV_STATUS_READY) {
		pr_err("IHK-SMP: error: "
		       "Device has already booted / is booting OS.\n");
		ret = -EBUSY;
		spin_unlock_irqrestore(&dev->lock, flags);
		goto out;
	}
	spin_unlock_irqrestore(&dev->lock, flags);

	if (os->status == BUILTIN_OS_STATUS_BOOTING) {
		pr_err("IHK-SMP: error: "
		       "Device has already booted / is booting OS.\n");
		ret = -EBUSY;
		goto out;
	}

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
		pr_err("IHK-SMP: error allocating NUMA mapping\n");
		ret = -ENOMEM;
		goto out;
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

	buffer_size = 0;
	/* Count number of memory chunks */
	list_for_each_entry(os_mem_chunk, &ihk_mem_used_chunks, list) {
		if (os_mem_chunk->os != ihk_os)
			continue;

		++nr_memory_chunks;
		buffer_size +=
			((((os_mem_chunk->size +
			    PAGE_SIZE * BITS_PER_LONG - 1) /
			   (PAGE_SIZE * BITS_PER_LONG)) *
			  sizeof(unsigned long)) +
			 sizeof(struct ihk_dump_page));
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
		pr_err("IHK-SMP: error: allocating boot parameter structure\n");
		ret = -ENOMEM;
		goto free_numa_mapping;
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
	os->param->osnum = ihk_host_os_get_index(ihk_os);
	os->param->linux_default_huge_page_shift =
		huge_page_order(&smp_ihk_hstates[*smp_ihk_default_hstate_idx])
		+ PAGE_SHIFT;
	os->nr_numa_nodes = nr_numa_nodes;

	ret = smp_ihk_arch_get_perf_event_map(os->param);
	if (ret) {
		pr_err("IHK-SMP: error: smp_ihk_arch_get_perf_event_map "
		       "returned %d", ret);
		goto free_param_pages;
	}

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

	set_dev_status(dev, BUILTIN_DEV_STATUS_BOOTING);

	__build_os_info(os);
	if (os->cpu_info.n_cpus < 1) {
		pr_err("IHK-SMP: error: There are no CPU to boot!\n");
		ret = -EINVAL;
		goto revert_dev_status;
	}
	os->boot_cpu = os->cpu_info.hw_ids[0];

	set_os_status(os, BUILTIN_OS_STATUS_BOOTING);

	dprint_var_x4(os->boot_cpu);
	dprint_var_x8(os->boot_rip);

	os->param->start = os->mem_start;
	os->param->end = os->mem_end;
	os->param->bootstrap_mem_end = os->bootstrap_mem_end;
	os->param->ident_table = ident_page_table;
	strncpy(os->param->kernel_args, os->kernel_args,
	        sizeof(os->param->kernel_args));

	os->param->msg_buffer = virt_to_phys(ihk_core_os->kmsg_buf_container->kmsg_buf);
	os->param->msg_buffer_size = sizeof(struct ihk_kmsg_buf); /* Note that it's used for map_fixed_area */
	dprintk("%s: msg_buffer=%lx,size=%ld\n", __FUNCTION__, os->param->msg_buffer, os->param->msg_buffer_size);

	os->param->ns_per_tsc = calc_ns_per_tsc();
	getnstimeofday(&now);
	os->param->boot_tsc = rdtsc();
	os->param->boot_sec = now.tv_sec;
	os->param->boot_nsec = now.tv_nsec;


	dprintf("boot cpu : %d, %lx, %lx, %lx, %lx\n",
	        os->boot_cpu, os->mem_start, os->mem_end, os->cpu_hw_ids_map.set[0],
	        os->param->dma_address
	);

	smp_ihk_setup_trampoline(os);

	dump_size = (buffer_size + PAGE_SIZE - 1) & PAGE_MASK;
	dump_pages_order = 0;
	while (((size_t)PAGE_SIZE << dump_pages_order) < dump_size)
		++dump_pages_order;

	dump_pages = alloc_pages(GFP_KERNEL | __GFP_ZERO, dump_pages_order);
	if (!dump_pages) {
		kfree(os);
		printk("IHK-SMP: error: allocating boot parameter structure\n");
		ret = -ENOMEM;
		goto revert_os_status;
	}

	dump_page = pfn_to_kaddr(page_to_pfn(dump_pages));

	if (dump_page) {

		dump_page_set_addr = (unsigned long)&os->param->dump_page_set;

		memset(dump_page,0,buffer_size);
		os->param->dump_page_set.count = nr_memory_chunks;
		os->param->dump_page_set.page_size = dump_size;
		os->param->dump_page_set.phy_page = __pa(dump_page);

		/* Perform initial setting of dump_page information */
		/* Turn on the BIT of the physical memory allocation range. */
		if (nr_memory_chunks) {

			i = 0;
			list_for_each_entry(os_mem_chunk, &ihk_mem_used_chunks, list) {
				const size_t csize = os_mem_chunk->size;

				if (i) {
					dump_page = (struct ihk_dump_page *)((char *)dump_page + ((dump_page->map_count * sizeof(unsigned long)) + sizeof(struct ihk_dump_page)));
				}

				dump_page->start = os_mem_chunk->addr;
				dump_page->map_count =
					((csize + PAGE_SIZE * BITS_PER_LONG - 1) /
					 (PAGE_SIZE * BITS_PER_LONG));
				map_end = ((csize + PAGE_SIZE - 1) >>
					   PAGE_SHIFT);

				for (index = 0; index < map_end; index++) {
					if(MAP_INDEX(index) >= dump_page->map_count) {
						printk("%s:used chunk is out of range(max:%ld): %ld\n", __FUNCTION__, dump_page->map_count, MAP_INDEX(index));
						break;
					}
					dump_page->map[MAP_INDEX(index)] |= (1UL << MAP_BIT(index));
				}

				i++;
			}
		}
	} else {
		os->param->dump_page_set.count = 0;
		os->param->dump_page_set.page_size = 0;
		dprintf("IHK-SMP: error: allocating dump_page_set(size:%ld)\n",buffer_size);
	}

	os->param->dump_page_set.completion_flag = IHK_DUMP_PAGE_SET_INCOMPLETE;

	printk("IHK-SMP: booting OS 0x%lx, calling smp_wakeup_secondary_cpu() \n", 
		(unsigned long)ihk_os);
	udelay(300);

	return smp_wakeup_secondary_cpu(os->boot_cpu, trampoline_phys);
	
	/* Never reach these.. */
	linux_numa_2_lwk_numa(os, 0);
	linux_cpu_2_lwk_cpu(os, 0);
	lwk_numa_2_linux_numa(os, 0);
	lwk_cpu_2_linux_cpu(os, 0);

	/* Error cases */
 revert_os_status:
	set_os_status(os, BUILTIN_OS_STATUS_LOADED);
 revert_dev_status:
	set_dev_status(dev, BUILTIN_DEV_STATUS_READY);
 free_param_pages:
	free_pages((unsigned long)pfn_to_kaddr(page_to_pfn(param_pages)),
		   param_pages_order);
 free_numa_mapping:
	kfree(os->numa_mapping);
 out:
	return ret;
}


static int smp_ihk_os_map_lwk(unsigned long phys)
{
	unsigned long flags;
	int vmap_area_taken = 0;

	/*
	 * Map in LWK image to Linux kernel space
	 */
	lwk_va = kmalloc(sizeof(*lwk_va), GFP_KERNEL);
	if (!lwk_va) {
		return -1;
	}

	spin_lock_irqsave(ihk_vmap_area_lock, flags);

	if ((vmap_area_taken = smp_ihk_arch_vmap_area_taken())) {
		kfree(lwk_va);
		lwk_va = NULL;
		pr_info("%s: ERROR: reserving LWK kernel memory virtual range\n",
				__func__);
	}
	else {
		lwk_va->va_start = IHK_SMP_MAP_KERNEL_START;
		lwk_va->va_end = MODULES_END;
		lwk_va->flags = 0;
		ihk___insert_vmap_area(lwk_va);
	}

	spin_unlock_irqrestore(ihk_vmap_area_lock, flags);

	if (vmap_area_taken)
		return -1;

	if (ihk_ioremap_page_range(IHK_SMP_MAP_KERNEL_START, MODULES_END,
				   phys, PAGE_KERNEL_EXEC) < 0) {
		pr_info("%s: error: mapping LWK to Linux kernel space\n",
				__func__);
	}

	return 0;
}

static int smp_ihk_os_vtop(ihk_os_t ihk_os,
	void *priv, unsigned long virt, unsigned long *phys)
{
	struct smp_os_data *os = priv;

	/* Part of LWK kernel image? (E.g., global variables) */
	if (virt > IHK_SMP_MAP_KERNEL_START && virt < MODULES_END) {
		unsigned long base_phys = (os->bootstrap_mem_start +
				IHK_SMP_LARGE_PAGE * 2 - 1) & IHK_SMP_LARGE_PAGE_MASK;

		*phys = base_phys + (virt - IHK_SMP_MAP_KERNEL_START);
		dprintf("%s: 0x%lx -> 0x%lx (IHK_SMP_MAP_KERNEL_START)\n",
			__func__, virt, *phys);
	}
	else {
		*phys = virt_to_phys((void *)virt);
		dprintf("%s: 0x%lx -> 0x%lx (dynamic)\n",
			__func__, virt, *phys);
	}

	return 0;
}

static int smp_ihk_os_load_file(ihk_os_t ihk_os, void *priv, const char *fn)
{
	int ret;
	struct smp_os_data *os = priv;
	struct file *file;
	loff_t pos = 0;
	long r;
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
		ret = -EINVAL;
		goto out;
	}

	printk("IHK-SMP: bootstrap addr: 0x%lx, chunk size: %lu @ NUMA: %d\n",
			os->bootstrap_mem_start,
			os->bootstrap_mem_end - os->bootstrap_mem_start,
			os->bootstrap_numa_id);

	if (!CORE_ISSET_ANY(&os->cpu_hw_ids_map) ||
			os->bootstrap_mem_end < os->bootstrap_mem_start) {
		printk("%s: OS is not ready to boot\n", __FUNCTION__);
		ret = -EINVAL;
		goto out;
	}

	spin_lock_irqsave(&os->lock, flags);
	if (os->status != BUILTIN_OS_STATUS_INITIAL) {
		printk("builtin: OS status is not initial.\n");
		spin_unlock_irqrestore(&os->lock, flags);
		ret = -EBUSY;
		goto out;

	}
	os->status = BUILTIN_OS_STATUS_LOADING;
	spin_unlock_irqrestore(&os->lock, flags);

	file = filp_open(fn, O_RDONLY, 0);
	if (IS_ERR(file)) {
		printk("open failed: %s\n", fn);
		ret = -ENOENT;
		goto revert_state;
	}

	elf64 = ihk_smp_map_virtual(os->bootstrap_mem_end - PAGE_SIZE, PAGE_SIZE);
	if (!elf64) {
		printk("error: ioremap() returns NULL\n");
		ret = -EINVAL;
		goto revert_state;
	}

	printk("IHK-SMP: loading ELF header for OS 0x%lx, phys=0x%lx\n",
		(unsigned long)ihk_os, os->bootstrap_mem_end - PAGE_SIZE);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)
	r = kernel_read(file, (char *)elf64, PAGE_SIZE, &pos);
#else
	r = kernel_read(file, pos, (char *)elf64, PAGE_SIZE);
#endif
	if (r <= 0) {
		pr_err("kernel_read failed: %ld\n", r);
		ihk_smp_unmap_virtual(elf64);
		fput(file);
		ret = r;
		goto revert_state;
	}
	if(elf64->e_ident[0] != 0x7f ||
	   elf64->e_ident[1] != 'E' ||
	   elf64->e_ident[2] != 'L' ||
	   elf64->e_ident[3] != 'F' ||
	   elf64->e_phoff + sizeof(Elf64_Phdr) * elf64->e_phnum > PAGE_SIZE){
		printk("kernel: BAD ELF\n");
		ihk_smp_unmap_virtual(elf64);
		fput(file);
		ret = -EINVAL;
		goto revert_state;
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
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)
			r = kernel_read(file, buf, l, &pos);
#else
			r = kernel_read(file, pos, buf, l);
			pos += r;
#endif
			if(r != PAGE_SIZE){
				memset(buf + r, '\0', PAGE_SIZE - r);
			}
			ihk_smp_unmap_virtual(buf);
			if (r <= 0) {
				pr_err("kernel_read failed: %ld\n", r);
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

	if ((ret = smp_ihk_os_map_lwk(phys))) {
		pr_info("%s: WARNING: smp_ihk_os_map_lwk failed: %d\n",
			__func__, ret);
	}

	if ((ret = smp_ihk_os_setup_startup(os, phys, entry))) {
		printk("%s: ERROR: smp_ihk_os_setup_startup failed (%d)\n", __FUNCTION__, ret);
		goto revert_state;
	}


	dump_bootstrap_mem_start = os->bootstrap_mem_start;
	ret = 0;

 revert_state:
	set_os_status(os, BUILTIN_OS_STATUS_INITIAL);
 out:
	return ret;
}

static int smp_ihk_os_load_mem(ihk_os_t ihk_os, void *priv, const char *buf,
                               unsigned long size, long offset)
{
	struct smp_os_data *os = priv;
	unsigned long phys, to_read, flags;
	void *virt;

	dprint_func_enter;

	/* We just load from the lowest address of the private memory */
	if (!CORE_ISSET_ANY(&os->cpu_hw_ids_map) || os->mem_end < os->mem_start) {
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

static int smp_ihk_os_unmap_lwk(void)
{
	if (lwk_va) {
		unsigned long flags;

		/* Unmap LWK from Linux kernel virtual */
		unmap_kernel_range_noflush(IHK_SMP_MAP_KERNEL_START,
				MODULES_END - IHK_SMP_MAP_KERNEL_START);

		spin_lock_irqsave(ihk_vmap_area_lock, flags);
		ihk___free_vmap_area(lwk_va);
		lwk_va = NULL;
		spin_unlock_irqrestore(ihk_vmap_area_lock, flags);
	}
	return 0;
}

static int smp_ihk_os_shutdown(ihk_os_t ihk_os, void *priv, int flag)
{
	struct smp_os_data *os = priv;
	struct builtin_device_data *dev = os->dev;
	int i, ret = 0;
	struct ihk_os_mem_chunk *os_mem_chunk = NULL;
	struct ihk_os_mem_chunk *next_chunk = NULL;
	struct chunk *mem_chunk;

	if(os->status == BUILTIN_OS_STATUS_SHUTDOWN) {
		eprintk("%s,already down\n", __FUNCTION__);
		return 0;
	}
	set_os_status(os, BUILTIN_OS_STATUS_SHUTDOWN);

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
	os->nr_cpus = 0;

	if ((ret = smp_ihk_os_unmap_lwk())) {
		printk("%s: ERROR: smp_ihk_os_unmap_lwk failed (%d)\n", __FUNCTION__, ret);
	}

	/* Free bootstrap page tables */
	if (os->boot_pt) {
		ihk_smp_free_page_tables(os->boot_pt);
		os->boot_pt = NULL;
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

	if (os->numa_mapping) {
		kfree(os->numa_mapping);
		os->numa_mapping = NULL;
	}

	if (os->param && os->param_pages_order) {
		free_pages((unsigned long)os->param, os->param_pages_order);
	}

	set_os_status(os, BUILTIN_OS_STATUS_INITIAL);
	set_dev_status(dev, BUILTIN_DEV_STATUS_READY);

	//kfree(os); /* done in destroy */

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

/* Called from host_driver.c */
static void smp_ihk_os_notify_hungup(ihk_os_t ihk_os, void *priv)
{
	struct smp_os_data *os = priv;
	set_os_status(os, BUILTIN_OS_STATUS_HUNGUP);
}

static int smp_ihk_os_get_num_numa_nodes(ihk_os_t ihk_os, void *priv)
{
	struct smp_os_data *os = priv;

	return os->mem_info.n_numa_nodes;
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

	strncpy(os->kernel_args, buf, sizeof(os->kernel_args) - 1);
	os->kernel_args[sizeof(os->kernel_args) - 1] = '\0';
	dprintk("%s,kernel_args=%s\n", __FUNCTION__, os->kernel_args);

	set_os_status(os, BUILTIN_OS_STATUS_INITIAL);

	return 0;
}

int ihk_smp_set_multi_intr_mode(ihk_os_t ihk_os, void *priv, int mode)
{
	unsigned long rpa;
	unsigned long size;
	int *multi_intr_mode;
	unsigned long pa;
	unsigned long psize;

	if (smp_ihk_os_get_special_addr(ihk_os, priv,
					IHK_SPADDR_MULTI_INTR_MODE,
					&rpa, &size)) {
		return -EINVAL;
	}

	psize = PAGE_SIZE;
	pa = smp_ihk_os_map_memory(ihk_os, priv, rpa, psize);
	multi_intr_mode = smp_ihk_map_virtual(ihk_os, priv, pa, psize,
					  NULL, 0);
	*multi_intr_mode = mode;
	smp_ihk_unmap_virtual(ihk_os, priv, multi_intr_mode, psize);
	smp_ihk_unmap_memory(ihk_os, priv, pa, psize);

	return 0;
}

int ihk_smp_set_nmi_mode(ihk_os_t ihk_os, void *priv, int mode)
{
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
			dprintk("%s: waiting for: %d, status: %d\n",
				__FUNCTION__, status, s);
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
	case IHK_SPADDR_RUSAGE:
		if (os->param->rusage) {
			*addr = os->param->rusage;
			*size = os->param->rusage_size;
			return 0;
		}
		break;
	case IHK_SPADDR_MULTI_INTR_MODE:
		if (os->param->multi_intr_mode_addr) {
			*addr = os->param->multi_intr_mode_addr;
			*size = sizeof(int);
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
	case IHK_SPADDR_MCKERNEL_DO_FUTEX:
		if (os->param->mckernel_do_futex) {
			*addr = os->param->mckernel_do_futex;
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
	int found = 0;

	/* XXX: Linear search? */
	list_for_each_entry(h, &builtin_interrupt_handlers, list) {
		if (h->func) {
			h->func(h->os, h->os_priv, h->priv);
			found = 1;
		}
	}
	
	if(!found) {
		kprintf("%s: ERROR: no handler registered\n", __FUNCTION__);
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
 * Assign CPUs in the array
 * NOTE: The cpus and num_cpus must be valid.
 */
static int __assign_cpus(ihk_os_t ihk_os, struct smp_os_data *os,
			int *cpus, int num_cpus)
{
	int cpu;
	int i;

	for (i = 0; i < num_cpus; i++) {
		cpu = cpus[i];
		dprintk(KERN_INFO "IHK-SMP: assigned CPU %d to OS %p\n",
			cpu, ihk_os);

		CORE_SET(ihk_smp_cpus[cpu].hw_id, os->cpu_hw_ids_map);
		set_bit(cpu_to_node(cpu), &os->numa_mask);

		ihk_smp_cpus[cpu].status = IHK_SMP_CPU_ASSIGNED;
		ihk_smp_cpus[cpu].os = ihk_os;
		ihk_smp_cpus[cpu].ikc_map_cpu = 0;

		os->cpu_mapping[os->nr_cpus] = cpu;
		os->cpu_hw_ids[os->nr_cpus] = ihk_smp_cpus[cpu].hw_id;
		os->nr_cpus++;
	}

	return 0;
}

static int smp_ihk_os_assign_cpu(ihk_os_t ihk_os, void *priv, unsigned long arg)
{
	int ret;
	int cpu;
	int i;
	struct smp_os_data *os = priv;
	cpumask_t cpus_to_assign;
	unsigned long flags;
	struct ihk_cpu_req req;
	int *req_cpus = NULL;
	char req_string[REQ_STR_MAXLEN];

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

	if (req.num_cpus == 0) {
		printk("%s: invalid request length\n", __FUNCTION__);
		return -EINVAL;
	}

	req_cpus = kmalloc(sizeof(int) * req.num_cpus, GFP_KERNEL);
	if (!req_cpus) {
		pr_err("%s: error: allocating request cpus\n", __func__);
		return -EINVAL;
	}

	if (copy_from_user(req_cpus, req.cpus, sizeof(int) * req.num_cpus)) {
		pr_err("%s: error: copying request cpus\n", __func__);
		ret = -EFAULT;
		goto out;
	}

	req_string[0] = '\0';
	cpu_array2str(req_string, sizeof(req_string), req.num_cpus, req_cpus);

	memset(&cpus_to_assign, 0, sizeof(cpus_to_assign));

	for (i = 0; i < req.num_cpus; i++) {
		if (req_cpus[i] < 0 || req_cpus[i] >= nr_cpu_ids) {
			pr_info("%s: error: CPU %d is out of range\n",
				__func__, req_cpus[i]);

			ret = -EINVAL;
			goto out;
		}
		cpumask_set_cpu(req_cpus[i], &cpus_to_assign);
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

	ret = __assign_cpus(ihk_os, os, req_cpus, req.num_cpus);
	if (ret) {
		pr_err("%s: error: assigning CPUs: %s\n", __func__, req_string);
		goto out;
	}

	pr_info("IHK-SMP: CPUs: %s assigned to OS %p\n", req_string, ihk_os);

out:
	kfree(req_cpus);
	return ret;
}

static int smp_ihk_os_release_cpu(ihk_os_t ihk_os, void *priv, unsigned long arg)
{
	int ret;
	int cpu;
	int i;
	struct smp_os_data *os = priv;
	cpumask_t cpus_to_release;
	unsigned long flags;
	struct ihk_cpu_req req;
	int *req_cpus = NULL;
	char req_string[REQ_STR_MAXLEN];

	spin_lock_irqsave(&os->lock, flags);
	if (os->status != BUILTIN_OS_STATUS_INITIAL) {
		spin_unlock_irqrestore(&os->lock, flags);
		pr_err("%s: error: os status: %d\n",
		       __func__, os->status);
		return -EBUSY;
	}
	spin_unlock_irqrestore(&os->lock, flags);

	if (copy_from_user(&req, (void *)arg, sizeof(req))) {
		printk("%s: error: copying request\n", __FUNCTION__);
		return -EFAULT;
	}

	if (req.num_cpus == 0) {
		printk("%s: invalid request length\n", __FUNCTION__);
		return -EINVAL;
	}

	req_cpus = kmalloc(sizeof(int) * req.num_cpus, GFP_KERNEL);
	if (!req_cpus) {
		pr_err("%s: error: allocating request cpus\n", __func__);
		return -EINVAL;
	}

	if (copy_from_user(req_cpus, req.cpus, sizeof(int) * req.num_cpus)) {
		pr_err("%s: error: copying request cpus\n", __func__);
		ret = -EFAULT;
		goto out;
	}

	req_string[0] = '\0';
	cpu_array2str(req_string, sizeof(req_string), req.num_cpus, req_cpus);

	memset(&cpus_to_release, 0, sizeof(cpus_to_release));

	for (i = 0; i < req.num_cpus; i++) {
		if (req_cpus[i] < 0 || req_cpus[i] >= nr_cpu_ids) {
			pr_info("%s: error: CPU %d is out of range\n",
				__func__, req_cpus[i]);

			ret = -EINVAL;
			goto out;
		}
		cpumask_set_cpu(req_cpus[i], &cpus_to_release);
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
		req_string, ihk_os);

	ret = 0;

out:
	kfree(req_cpus);
	return ret;
}

static int smp_ihk_os_query_cpu(ihk_os_t ihk_os, void *priv, unsigned long arg)
{
	int i, ret;
	int idx;
	struct ihk_cpu_req req;
	struct ihk_cpu_req *res = (struct ihk_cpu_req *)arg;
	int *res_cpus = NULL;
	struct smp_os_data *os = priv;

	if (copy_from_user(&req, (void *)arg, sizeof(req))) {
		pr_err("%s: error: copying request\n", __func__);
		return -EFAULT;
	}

	if (req.num_cpus != os->nr_cpus) {
		pr_err("%s: error: #cpu requested (%d) != actual (%d)\n",
		       __func__, req.num_cpus, os->nr_cpus);
		ret = -EINVAL;
		goto out;
	}

	if (!(res_cpus = kmalloc(sizeof(int) * req.num_cpus, GFP_KERNEL))) {
		pr_err("%s: error: allocating res_cpus\n", __func__);
		ret = -ENOMEM;
		goto out;
	}

	/* Respect the order of cpus specified when assigining them
	   e.g. 0,2,1,3 */
	for (i = 0, idx = 0; i < os->nr_cpus; ++i) {
		res_cpus[idx] = os->cpu_mapping[i];
		idx++;
	}

	if (req.num_cpus > 0) {
		if (copy_to_user(req.cpus, res_cpus,
				 sizeof(int) * req.num_cpus)) {
			pr_err("%s: error: copying CPU array to user-space\n",
			       __func__);
			ret = -EFAULT;
			goto out;
		}
	}

	if (copy_to_user(&res->num_cpus, &req.num_cpus, sizeof(int))) {
		pr_err("%s: error: copying numer of CPUs  to user-space\n",
		       __func__);
		ret = -EFAULT;
		goto out;
	}

	ret = 0;
out:
	kfree(res_cpus);
	return ret;
}

static int smp_ihk_os_get_num_cpus(ihk_os_t ihk_os, void *priv)
{
	struct smp_os_data *os = priv;

	return os->nr_cpus;
}


static int smp_ihk_os_set_ikc_map(ihk_os_t ihk_os, void *priv, unsigned long arg)
{
	int ret = 0;
	int i;
	struct smp_os_data *os = priv;
	struct ihk_ikc_req req;
	unsigned long flags;
	int *req_src_cpus = NULL;
	int *req_dst_cpus = NULL;
	char req_string[REQ_STR_MAXLEN];

	dprintk("%s,set_ikc_map\n", __func__);

	if (copy_from_user(&req, (void *)arg, sizeof(req))) {
		pr_err("%s: error: copying request\n", __func__);
		return -EFAULT;
	}

	if (req.num_cpus == 0) {
		printk("%s: invalid request length\n", __FUNCTION__);
		return -EINVAL;
	}

	spin_lock_irqsave(&os->lock, flags);
	if (os->status != BUILTIN_OS_STATUS_INITIAL) {
		spin_unlock_irqrestore(&os->lock, flags);
		ret = -EBUSY;
		goto out;
	}
	spin_unlock_irqrestore(&os->lock, flags);

	req_src_cpus = kmalloc(sizeof(int) * req.num_cpus, GFP_KERNEL);
	if (!req_src_cpus) {
		pr_err("%s: error: allocating request src_cpus\n", __func__);
		return -EINVAL;
	}

	if (copy_from_user(req_src_cpus, req.src_cpus,
					   sizeof(int) * req.num_cpus)) {
		pr_err("%s: error: copying request src_cpus\n", __func__);
		ret = -EFAULT;
		goto out;
	}

	req_dst_cpus = kmalloc(sizeof(int) * req.num_cpus, GFP_KERNEL);
	if (!req_dst_cpus) {
		pr_err("%s: error: allocating request dst_cpus\n", __func__);
		return -EINVAL;
	}

	if (copy_from_user(req_dst_cpus, req.dst_cpus,
					   sizeof(int) * req.num_cpus)) {
		pr_err("%s: error: copying request dst_cpus\n", __func__);
		ret = -EFAULT;
		goto out;
	}

	req_string[0] = '\0';
	if (ikc_array2str(req_string, sizeof(req_string), req.num_cpus,
			  req_src_cpus, req_dst_cpus)) {
		pr_warn("%s: failed to build ikc_map string\n", __func__);
	}

	for (i = 0; i < req.num_cpus; i++) {
		int src_cpu = req_src_cpus[i];
		int dst_cpu = req_dst_cpus[i];

		pr_debug("%s: src: %d, dst: %d\n",
			__func__, src_cpu, dst_cpu);

		if (src_cpu < 0 || src_cpu >= SMP_MAX_CPUS) {
			pr_err("%s: error: src cpu %d is out of range, "
			       "SMP_MAX_CPUS: %d\n",
			       __func__, src_cpu, SMP_MAX_CPUS);
			ret = -EINVAL;
			goto out;
		}

		if (ihk_smp_cpus[src_cpu].status != IHK_SMP_CPU_ASSIGNED) {
			pr_err("%s: error: src cpu %d is not assigned\n",
			       __func__, src_cpu);
			ret = -EINVAL;
			goto out;
		}

		if (dst_cpu < 0 || dst_cpu >= SMP_MAX_CPUS) {
			pr_err("%s: error: dst cpu %d is out of range, "
			       "SMP_MAX_CPUS: %d\n",
			       __func__, dst_cpu, SMP_MAX_CPUS);
			ret = -EINVAL;
			goto out;
		}

		if (ihk_smp_cpus[dst_cpu].status == IHK_SMP_CPU_ASSIGNED) {
			pr_err("%s: error: dst cpu %d is assigned\n",
			       __func__, dst_cpu);
			ret = -EINVAL;
			goto out;
		}

		if (!cpu_present(dst_cpu)) {
			pr_err("%s: error: dst cpu %d isn't present\n",
			       __func__, dst_cpu);
			ret = -EINVAL;
			goto out;
		}

		if (!cpu_online(dst_cpu)) {
			pr_err("%s: error: dst cpu %d isn't online\n",
			       __func__, dst_cpu);
			ret = -EINVAL;
			goto out;
		}

		ihk_smp_cpus[src_cpu].ikc_map_cpu = dst_cpu;
	}

	/* Mapping has been requested */
	if (smp_ihk_os_check_ikc_map(ihk_os) == 0) {
		os->cpu_ikc_mapped = 1;
	}

	for (i = 0; i < SMP_MAX_CPUS; i++) {
		if ((ihk_smp_cpus[i].status != IHK_SMP_CPU_ASSIGNED) ||
				(ihk_smp_cpus[i].os != ihk_os)) {
			continue;
		}
		pr_info("%s: IKC IRQ routing: %d -> %d\n",
				__func__, i, ihk_smp_cpus[i].ikc_map_cpu);
	}

out:
	/* In case of no mapped, restore default setting */
	if (os->cpu_ikc_mapped != 1) {
		for (i = 0; i < SMP_MAX_CPUS; i++) {
			if ((ihk_smp_cpus[i].status != IHK_SMP_CPU_ASSIGNED) ||
			    (ihk_smp_cpus[i].os != ihk_os)) {
				continue;
			}
			ihk_smp_cpus[i].ikc_map_cpu = 0;
		}
	}

	kfree(req_src_cpus);
	kfree(req_dst_cpus);

	return ret;
}

static int smp_ihk_os_get_ikc_map(ihk_os_t ihk_os, void *priv, unsigned long arg)
{
	int src, ret = 0, idx = 0;
	struct ihk_ikc_req req;
	struct ihk_ikc_req *res = (struct ihk_ikc_req *)arg;
	int *res_src_cpus = NULL;
	int *res_dst_cpus = NULL;

	if (copy_from_user(&req, (void *)arg, sizeof(req))) {
		pr_err("%s: error: copying request\n", __func__);
		return -EFAULT;
	}

	if (req.num_cpus == 0) {
		pr_err("%s: invalid request length\n", __func__);
		return -EINVAL;
	}

	res_src_cpus = kmalloc(sizeof(int) * req.num_cpus, GFP_KERNEL);
	if (!res_src_cpus) {
		pr_err("%s: error: allocating request src_cpus\n", __func__);
		return -EINVAL;
	}

	res_dst_cpus = kmalloc(sizeof(int) * req.num_cpus, GFP_KERNEL);
	if (!res_dst_cpus) {
		pr_err("%s: error: allocating request dst_cpus\n", __func__);
		return -EINVAL;
	}

	if (copy_from_user(res_src_cpus, req.src_cpus,
					   sizeof(int) * req.num_cpus)) {
		pr_err("%s: error: copying request src_cpus\n", __func__);
		ret = -EFAULT;
		goto out;
	}

	if (copy_from_user(res_dst_cpus, req.dst_cpus,
					   sizeof(int) * req.num_cpus)) {
		pr_err("%s: error: copying request dst_cpus\n", __func__);
		ret = -EFAULT;
		goto out;
	}

	for (src = 0; src < SMP_MAX_CPUS; ++src) {
		if (ihk_smp_cpus[src].status != IHK_SMP_CPU_ASSIGNED)
			continue;
		if (ihk_smp_cpus[src].os != ihk_os)
			continue;

		res_src_cpus[idx] = src;
		res_dst_cpus[idx] = ihk_smp_cpus[src].ikc_map_cpu;
		idx++;

		if (idx > req.num_cpus) {
			pr_err("%s: error: query_space is not large enough\n",
				__func__);
			ret = -EINVAL;
			goto out;
		}
	}

	if (idx > 0) {
		if (copy_to_user(req.src_cpus, res_src_cpus,
						 sizeof(int) * idx)) {
			pr_err("%s: error: copying src_cpus to user-space\n",
				__func__);
			ret = -EFAULT;
			goto out;
		}
		if (copy_to_user(req.dst_cpus, res_dst_cpus,
						 sizeof(int) * idx)) {
			pr_err("%s: error: copying dst_cpus to user-space\n",
				__func__);
			ret = -EFAULT;
			goto out;
		}
	}

	if (copy_to_user(&res->num_cpus, &idx, sizeof(int))) {
		pr_err("%s: error: copying num_cpus to user-space\n",
			__func__);
		ret = -EFAULT;
		goto out;
	}

	ret = 0;

out:
	kfree(res_src_cpus);
	kfree(res_dst_cpus);
	return ret;
}

static int smp_ihk_os_get_buildid(ihk_os_t ihk_os, void *priv, unsigned long arg)
{
	char buildid[] = BUILDID;
	if (copy_to_user((void*)arg, buildid, sizeof(buildid))) {
		return -EFAULT;
	}
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
	size_t want = mem_size;
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
			/* Special condition for "all" */
			if (want == IHK_SMP_MEM_ALL) {
				break;
			}

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
					struct page *head = compound_head(pg);
					size_t comp_size = PAGE_SIZE << compound_order(head);

					if ((page_to_phys(head) + comp_size) <
							mem_chunk_max->addr + mem_chunk_max->size) {
						off_t comp_end_offset = comp_size -
							(page_to_phys(pg) - page_to_phys(head));

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

static int _smp_ihk_os_release_mem(ihk_os_t ihk_os, size_t size, int numa_id);
static int smp_ihk_os_assign_mem(ihk_os_t ihk_os, void *priv, unsigned long arg)
{
	struct smp_os_data *os = priv;
	unsigned long flags;
	int ret = 0, i;
	struct ihk_mem_req req;
	size_t *req_sizes = NULL;
	int *req_numa_ids = NULL;
	int failed_index = 0;

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

	if (req.num_chunks == 0) {
		printk("%s: invalid request length\n", __FUNCTION__);
		return -EINVAL;
	}

	req_sizes = kmalloc(sizeof(size_t) * req.num_chunks, GFP_KERNEL);
	if (!req_sizes) {
		pr_err("%s: error: allocating request sizes\n", __func__);
		return -EINVAL;
	}

	req_numa_ids = kmalloc(sizeof(int) * req.num_chunks, GFP_KERNEL);
	if (!req_numa_ids) {
		pr_err("%s: error: allocating request numa_ids\n",
			__func__);
		return -EINVAL;
	}

	if (copy_from_user(req_sizes, req.sizes,
					   sizeof(size_t) * req.num_chunks)) {
		pr_err("%s: error: copying request sizes\n", __func__);
		ret = -EFAULT;
		goto out;
	}

	if (copy_from_user(req_numa_ids, req.numa_ids,
					   sizeof(int) * req.num_chunks)) {
		pr_err("%s: error: copying request numa_ids\n", __func__);
		ret = -EFAULT;
		goto out;
	}

	for (i = 0; i < req.num_chunks; i++) {
		ret = __smp_ihk_os_assign_mem(ihk_os, os, req_sizes[i],
				req_numa_ids[i]);
		if (ret != 0) {
			printk("IHK-SMP: os_assign_mem: error: assigning memory chunk\n");
			failed_index = i;
			goto out;
		}
	}

out:
	/* Release all when failed */
	for (i = 0; i < failed_index; i++) {
		_smp_ihk_os_release_mem(ihk_os, req_sizes[i], req_numa_ids[i]);
	}

	kfree(req_sizes);
	kfree(req_numa_ids);
	return ret;
}

static int _smp_ihk_os_release_mem(ihk_os_t ihk_os, size_t size, int numa_id)
{
	int ret;
	struct ihk_os_mem_chunk *os_mem_chunk = NULL;
	struct ihk_os_mem_chunk *next_chunk = NULL;
	struct chunk *mem_chunk;

	list_for_each_entry_safe(os_mem_chunk, next_chunk,
				 &ihk_mem_used_chunks, list) {

		if (os_mem_chunk->os != ihk_os
		    || os_mem_chunk->size != size
		    || os_mem_chunk->numa_id != numa_id) {
			continue;
		}

		list_del(&os_mem_chunk->list);

		mem_chunk = (struct chunk *)phys_to_virt(os_mem_chunk->addr);
		mem_chunk->addr = os_mem_chunk->addr;
		mem_chunk->size = os_mem_chunk->size;
		mem_chunk->numa_id = os_mem_chunk->numa_id;
		INIT_LIST_HEAD(&mem_chunk->chain);

		pr_info("IHK-SMP: chunk 0x%lx - 0x%lx"
		       " (len: %lu) @ NUMA node: %d is returned to IHK\n",
		       mem_chunk->addr, mem_chunk->addr + mem_chunk->size,
		       mem_chunk->size, mem_chunk->numa_id);

		add_free_mem_chunk(mem_chunk);

		kfree(os_mem_chunk);

		ret = 0;
		goto out;
	}

	ret = -EINVAL;
 out:
	return ret;
}

static int smp_ihk_os_release_mem(ihk_os_t ihk_os, void *priv, unsigned long arg)
{
	struct smp_os_data *os = priv;
	unsigned long flags;
	int ret = -EINVAL, i;
	struct ihk_mem_req req;
	size_t *req_sizes = NULL;
	int *req_numa_ids = NULL;

	spin_lock_irqsave(&os->lock, flags);
	if (os->status != BUILTIN_OS_STATUS_INITIAL) {
		spin_unlock_irqrestore(&os->lock, flags);
		return -EBUSY;
	}
	spin_unlock_irqrestore(&os->lock, flags);

	ret = copy_from_user(&req, (void *)arg, sizeof(req));
	if (ret) {
		pr_err("%s: error: copy_from_user request\n",
		       __func__);
		goto out;
	}

	if (req.num_chunks == 0) {
		ret = 0;
		goto out;
	}

	req_sizes = kmalloc(sizeof(size_t) * req.num_chunks, GFP_KERNEL);
	if (req_sizes == NULL) {
		pr_err("%s: error: allocating sizes\n",
		       __func__);
		ret = -ENOMEM;
		goto out;
	}

	req_numa_ids = kmalloc(sizeof(int) * req.num_chunks, GFP_KERNEL);
	if (req_numa_ids == NULL) {
		pr_err("%s: error: allocating NUMA ids\n",
		       __func__);
		ret = -ENOMEM;
		goto out;
	}

	ret = copy_from_user(req_sizes, req.sizes,
			sizeof(size_t) * req.num_chunks);
	if (ret) {
		pr_err("%s: error: copy_from_user sizes\n",
		       __func__);
		goto out;
	}

	ret = copy_from_user(req_numa_ids, req.numa_ids,
			sizeof(int) * req.num_chunks);
	if (ret) {
		pr_err("%s: error: copy_from_user NUMA ids\n",
		       __func__);
		goto out;
	}

	/* Drop specified memory chunks */
	for (i = 0; i < req.num_chunks; i++) {
		ret = _smp_ihk_os_release_mem(ihk_os, req_sizes[i],
					      req_numa_ids[i]);
		if (ret) {
			pr_err("%s: error: _smp_ihk_os_release_mem"
			       " returned %d\n",
			       __func__, ret);
			goto out;
		}
	}

	ret = 0;
 out:
	kfree(req_sizes);
	kfree(req_numa_ids);
	return ret;
}

static int smp_ihk_os_query_mem(ihk_os_t ihk_os, void *priv, unsigned long arg)
{
	int ret, num_chunks = 0, idx = 0;
	struct ihk_mem_req req;
	struct ihk_mem_req *res = (struct ihk_mem_req *)arg;
	struct ihk_os_mem_chunk *os_mem_chunk;
	size_t *query_res_size = NULL;
	int *query_res_numa_id = NULL;

	if (copy_from_user(&req, (void *)arg, sizeof(req))) {
		pr_err("%s: error: copying request\n", __func__);
		return -EFAULT;
	}

	/* Count memory chunks */
	list_for_each_entry(os_mem_chunk, &ihk_mem_used_chunks, list) {
		if (os_mem_chunk->os != ihk_os)
			continue;
		num_chunks++;
	}

	if (req.num_chunks == 0) {
		/* Get assigned num chunks */
		if (copy_to_user(&res->num_chunks, &num_chunks, sizeof(int))) {
			pr_err("%s: error: copying mem num_chunks to user-space\n",
				__func__);
			ret = -EFAULT;
			goto out;
		}
		ret = 0;
		goto out;
	}
	else if (num_chunks > req.num_chunks) {
		pr_err("%s: error: query_space is not large enough\n",
			__func__);
		ret = -EINVAL;
		goto out;
	}

	if (!(query_res_size = kmalloc(sizeof(size_t) * req.num_chunks,
			GFP_KERNEL))) {
		pr_err("%s: error: allocating query_res_size\n",
			__func__);
		ret = -ENOMEM;
		goto out;
	}

	if (!(query_res_numa_id = kmalloc(sizeof(int) * req.num_chunks,
			GFP_KERNEL))) {
		pr_err("%s: error: allocating query_res_numa_id\n",
			__func__);
		ret = -ENOMEM;
		goto out;
	}

	/* Collect memory information */
	list_for_each_entry(os_mem_chunk, &ihk_mem_used_chunks, list) {
		if (os_mem_chunk->os != ihk_os)
			continue;

		query_res_size[idx] = os_mem_chunk->size;
		query_res_numa_id[idx] = os_mem_chunk->numa_id;
		idx++;

	}

	if (idx > 0) {
		if (copy_to_user(req.sizes, query_res_size,
						 sizeof(size_t) * idx)) {
			pr_err("%s: error: copying mem sizes to user-space\n",
				__func__);
			ret = -EFAULT;
			goto out;
		}
		if (copy_to_user(req.numa_ids, query_res_numa_id,
						 sizeof(int) * idx)) {
			pr_err("%s: error: copying mem numa_ids to user-space\n",
				__func__);
			ret = -EFAULT;
			goto out;
		}
	}

	if (copy_to_user(&res->num_chunks, &idx, sizeof(int))) {
		pr_err("%s: error: copying mem num_chunks to user-space\n",
			__func__);
		ret = -EFAULT;
		goto out;
	}

	ret = 0;

out:
	kfree(query_res_size);
	kfree(query_res_numa_id);
	return ret;
}

static int smp_ihk_os_freeze(ihk_os_t ihk_os, void *priv)
{
	smp_ihk_os_send_multi_intr(ihk_os, priv, 1);
	return 0;
}

static int smp_ihk_os_thaw(ihk_os_t ihk_os, void *priv)
{
	smp_ihk_os_send_multi_intr(ihk_os, priv, 2);
	return 0;
}

static void smp_ihk_os_panic_notifier(ihk_os_t ihk_os, void *priv)
{
	struct smp_os_data *os = priv;
	struct ihk_dump_page *dump_page = NULL;
	unsigned long map_start;
	unsigned long i,j,k;
	struct page *pg;

	smp_ihk_os_send_nmi(ihk_os, priv, 0);

	while (os->param->dump_page_set.completion_flag !=
	       IHK_DUMP_PAGE_SET_COMPLETED) {

		cpu_relax();

	}

	if (os->param->dump_level == DUMP_LEVEL_USER_UNUSED_EXCLUDE) {

		dump_page = phys_to_virt((unsigned long)os->param->dump_page_set.phy_page);

		for (i = 0; i < os->param->dump_page_set.count; i++) {
			if (i) {
				dump_page = (struct ihk_dump_page *)((char *)dump_page + ((dump_page->map_count * sizeof(unsigned long)) + sizeof(struct ihk_dump_page)));
			}

			for (j = 0; j < dump_page->map_count; j++) {
				for (k = 0; k < 64; k++) {
					if (!((dump_page->map[j] >> k) & 0x1)) {
						map_start = (unsigned long)(dump_page->start + (j << (PAGE_SHIFT+6)));
						map_start = map_start + (k << PAGE_SHIFT);
						pg = virt_to_page(phys_to_virt(map_start));
						pg->mapping += PAGE_MAPPING_ANON;
					}
				}
			}
		}
	}

	return;
}

static struct ihk_os_ops smp_ihk_os_ops = {
	.load_mem = smp_ihk_os_load_mem,
	.load_file = smp_ihk_os_load_file,
	.boot = smp_ihk_os_boot,
	.shutdown = smp_ihk_os_shutdown,
	.alloc_resource = smp_ihk_os_alloc_resource,
	.query_status = smp_ihk_os_query_status,
	.notify_hungup = smp_ihk_os_notify_hungup,
	.get_num_numa_nodes = smp_ihk_os_get_num_numa_nodes,
	.wait_for_status = smp_ihk_os_wait_for_status,
	.set_kargs = smp_ihk_os_set_kargs,
	.dump = smp_ihk_os_dump,
	.issue_interrupt = smp_ihk_os_issue_interrupt,
	.send_multi_intr = smp_ihk_os_send_multi_intr,
	.send_nmi = smp_ihk_os_send_nmi,
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
	.set_ikc_map = smp_ihk_os_set_ikc_map,
	.get_ikc_map = smp_ihk_os_get_ikc_map,
	.get_buildid = smp_ihk_os_get_buildid,
	.get_num_cpus = smp_ihk_os_get_num_cpus,
	.query_cpu = smp_ihk_os_query_cpu,
	.assign_mem = smp_ihk_os_assign_mem,
	.release_mem = smp_ihk_os_release_mem,
	.query_mem = smp_ihk_os_query_mem,
	.freeze = smp_ihk_os_freeze,
	.thaw = smp_ihk_os_thaw,
	.panic_notifier = smp_ihk_os_panic_notifier,
	.vtop = smp_ihk_os_vtop,
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
	os->boot_pt = NULL;

	return 0;
}

static int smp_ihk_destroy_os(ihk_device_t ihk_dev, void *ihk_dev_priv,
							  ihk_os_t ihk_os, void *ihk_os_priv)
{
	struct smp_os_data *smp_os = ihk_os_priv;
	kfree(smp_os);
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
			pr_warn("WARNING: ihk_smp_map_virtual(%lx, %lx) returned NULL!\n",
				phys, size);
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

#define RESERVE_MEM_FAILED_ATTEMPTS 1
//#define USE_TRY_TO_FREE_PAGES

static int __ihk_smp_reserve_mem(size_t ihk_mem, int numa_id,
				 int min_chunk_size,
				 int max_size_ratio_all,
				 int timeout)
{
	int order = get_order(IHK_SMP_CHUNK_BASE_SIZE);
	size_t want = ihk_mem;
	size_t allocated;
	size_t available;
	struct chunk *p;
	struct chunk *q;
	int ret = 0;
	struct rb_root tmp_chunks = RB_ROOT;
	nodemask_t nodemask;
	int i;
	int order_limit = get_order(min_chunk_size);
#ifdef USE_TRY_TO_FREE_PAGES
	unsigned long (*__try_to_free_pages)(struct zonelist *zonelist, int order,
				gfp_t gfp_mask, nodemask_t *nodemask) = NULL;
#endif // USE_TRY_TO_FREE_PAGES
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,19,0)
	void (*__drain_all_pages)(struct zone *) = NULL;
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(3,19,0) */
	void (*__drain_all_pages)(void) = NULL;
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3,19,0) */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
	size_t (*__sum_zone_node_page_state)(int node,
					     enum zone_stat_item item);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
	size_t (*__node_page_state)(int node, enum zone_stat_item item);
#endif
	int failed_free_attempts = 0;
	unsigned long res_start = get_seconds();
#ifdef CONFIG_MOVABLE_NODE
	bool *__movable_node_enabled = NULL;
#endif

	if (order_limit < 0 || order_limit > MAX_ORDER) {
		pr_err("IHK-SMP: error: invalid order_limit (%d)\n",
		       order_limit);
		ret = -EINVAL;
		goto out;
	}

	if (!node_online(numa_id)) {
		pr_err("IHK-SMP: error: NUMA node %d isn't online\n",
		       numa_id);
		ret = -EINVAL;
		goto out;
	}

	memset(&nodemask, 0, sizeof(nodemask));
	__node_set(numa_id, &nodemask);

	dprintk(KERN_INFO "IHK-SMP: __ihk_smp_reserve_mem: %lu bytes\n", ihk_mem);

#ifdef USE_TRY_TO_FREE_PAGES
	__try_to_free_pages = (unsigned long (*)
			(struct zonelist *, int, gfp_t, nodemask_t *))
			kallsyms_lookup_name("try_to_free_pages");
#endif // USE_TRY_TO_FREE_PAGES
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,19,0)
	__drain_all_pages = (void (*)(struct zone *))
			kallsyms_lookup_name("drain_all_pages");
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(3,19,0) */
	__drain_all_pages = (void (*)(void))
			kallsyms_lookup_name("drain_all_pages");
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3,19,0) */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
	__sum_zone_node_page_state = (void *)
			kallsyms_lookup_name("sum_zone_node_page_state");
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
	__node_page_state = (void *)kallsyms_lookup_name("node_page_state");
#endif

#ifdef CONFIG_MOVABLE_NODE
	__movable_node_enabled =
		(bool *)kallsyms_lookup_name("movable_node_enabled");
#endif

	if (__drain_all_pages) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,19,0)
		__drain_all_pages(NULL);
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(3,19,0) */
		__drain_all_pages();
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3,19,0) */
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

	if (want != IHK_SMP_MEM_ALL) {
		want = (ihk_mem + ((PAGE_SIZE << order) - 1))
			& ~((PAGE_SIZE << order) - 1);
	}
	dprintk("%s: ihk_mem: %lu, want: %lu\n", __FUNCTION__, ihk_mem, want);
	allocated = 0;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
	available = __sum_zone_node_page_state(numa_id, NR_FREE_PAGES)
				<< PAGE_SHIFT;
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
	available = __node_page_state(numa_id, NR_FREE_PAGES) << PAGE_SHIFT;
#else
	available = (size_t)node_page_state(numa_id, NR_FREE_PAGES) << PAGE_SHIFT;
#endif
	printk("%s: NUMA %d (online nodes: %d), free mem: %lu bytes\n",
		__FUNCTION__, numa_id, num_online_nodes(), available);

retry:
	/* Allocate and merge pages until we get a contigous area
	 * or run out of free memory. Keep the longest areas */
	while (max_size_mem_chunk(&tmp_chunks) < want) {
		struct page *pg = NULL;

		/*
		 * Do not grab more than the specified % of available
		 * when requested "all" or when allocating from NUMA 0
		 * to avoid Linux crashing...
		 */
		if ((numa_id == 0 && allocated > (available * 95 / 100)) ||
		    (want == IHK_SMP_MEM_ALL &&
		     allocated > (available * max_size_ratio_all / 100))) {
			pr_info("%s: almost all of NUMA %d taken, breaking"
			       " allocation loop (current order: %d)..\n",
			       __func__, numa_id, order);
			goto pre_out;
		}

		pg = __alloc_pages_nodemask(
				GFP_KERNEL | __GFP_COMP | __GFP_NOWARN |
				__GFP_NORETRY | __GFP_THISNODE,
				//| __GFP_REPEAT,
				order,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 13, 0)
				numa_id,
#else
				node_zonelist(numa_id, GFP_KERNEL | __GFP_COMP),
#endif
				&nodemask);

#ifdef CONFIG_MOVABLE_NODE
		/* Try movable pages if supported */
		if (!pg && __movable_node_enabled && *__movable_node_enabled) {
			pg = __alloc_pages_nodemask(
					__GFP_MOVABLE | __GFP_HIGHMEM |
					__GFP_COMP | __GFP_NOWARN |
					__GFP_NORETRY | __GFP_THISNODE,
					//| __GFP_REPEAT,
					order,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 13, 0)
					numa_id,
#else
					node_zonelist(numa_id, __GFP_COMP),
#endif
					&nodemask);
		}
#endif

		if (!pg) {
#ifdef USE_TRY_TO_FREE_PAGES
			int freed_pages;
#endif // USE_TRY_TO_FREE_PAGES

			if (__drain_all_pages) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,19,0)
				__drain_all_pages(NULL);
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(3,19,0) */
				__drain_all_pages();
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3,19,0) */
			}

#ifdef USE_TRY_TO_FREE_PAGES
			/*
			 * Don't stress NUMA node 0, unless it is the only one
			 */
			if ((num_online_nodes() > 1 && numa_id > 0) &&
					(__try_to_free_pages &&
					 failed_free_attempts < RESERVE_MEM_FAILED_ATTEMPTS)) {

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
#endif // USE_TRY_TO_FREE_PAGES

			/*
			 * We ran out of memory using the current order of compound
			 * pages, decrease order and try to grab smaller pieces.
			 */
			if (order > order_limit) {
				--order;
				failed_free_attempts = 0;
				dprintk("%s: order decreased to %d\n", __FUNCTION__, order);

				/* Do not spend more than timeout secs on
				 * reservation
				 */
				if ((get_seconds() - res_start) < timeout) {
					goto retry;
				}
			}

pre_out:
			/*
			 * Otherwise, we may have run out of memory altogether before
			 * finding a single contigous chunk, but do we have enough in
			 * multiple chunks?
			 */
			if (allocated >= want || want == IHK_SMP_MEM_ALL) break;

			printk(KERN_ERR "IHK-SMP: error: __alloc_pages_node() failed\n");

			ret = -ENOMEM;
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

		/* Is the chunk bigger than what we need? */
		if (allocated + max > want) {
			struct page *leftover_page;
			struct chunk *leftover = (struct chunk *)(phys_to_virt(p->addr) +
				(want - allocated));

			/* Not in front of compound page? */
			leftover_page = virt_to_page(leftover);
			if (PageCompound(leftover_page) && !PageHead(leftover_page)) {
				struct page *head = compound_head(leftover_page);
				leftover = (struct chunk *)
					(phys_to_virt(page_to_phys(head)) +
					 (PAGE_SIZE << compound_order(head)));

				printk("%s: adjusted leftover chunk to compound "
						"page border: 0x%llx:%lu\n",
						__FUNCTION__,
						page_to_phys(head),
						(PAGE_SIZE << compound_order(head)));
			}

			/* Only if there is really something left.. */
			if (virt_to_phys(leftover) < p->addr + max) {
				leftover->addr = virt_to_phys(leftover);
				leftover->size = p->addr + max - leftover->addr;
				leftover->numa_id = p->numa_id;
				__mem_chunk_insert(&tmp_chunks, leftover);

				/* Update original chunk */
				max = (leftover->addr - p->addr);
				p->size = max;

				printk("%s: leftover chunk from allocation: 0x%lx:%lu\n",
					__FUNCTION__,
					leftover->addr,
					leftover->size);
			}
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

	pr_info("%s: want: %ld, allocated: %ld\n",
	       __func__, want, allocated);


	ret = 0;

out:
	/* Free leftover tmp_chunks */
	__smp_ihk_free_mem_from_rbtree(&tmp_chunks);

	return ret;
}

static void __ihk_smp_release_chunk(struct chunk *mem_chunk)
{
	unsigned long size_left;
	unsigned long va;
	unsigned long pa = mem_chunk->addr;

	va = (unsigned long)phys_to_virt(pa);
	size_left = mem_chunk->size;
	while (size_left > 0) {
		int order;
		size_t order_size;
		struct page *page = virt_to_page(va);

		if (!PageCompound(page) || !PageHead(page)) {
			dprintk(KERN_ERR "%s: WARNING: page is not compound or not head"
				", freeing single page\n",
				__func__);
			free_page(va);
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
				__func__, order_size - size_left);
			size_left = 0;
		}
	}
}

static int __ihk_smp_release_mem(size_t ihk_mem, int numa_id)
{
	int ret;
	struct chunk *mem_chunk;
	struct chunk *mem_chunk_next;

	list_for_each_entry_safe(mem_chunk,
			mem_chunk_next, &ihk_mem_free_chunks, chain) {
		if(mem_chunk->size != ihk_mem || mem_chunk->numa_id != numa_id) {
			continue;
		}

		list_del(&mem_chunk->chain);
		__ihk_smp_release_chunk(mem_chunk);
		pr_info("IHK-SMP: chunk 0x%lx - 0x%lx"
			" (len: %lu) @ NUMA node: %d is released\n",
			mem_chunk->addr, mem_chunk->addr + mem_chunk->size,
			mem_chunk->size, mem_chunk->numa_id);

		ret = 0;
		goto out;
	}

	ret = -EINVAL;
 out:
	return ret;
}

/* We want to balance the amounts of memory reserved across NUMA-nodes
 * while the total amount exceeds the specified by the resource manager (RM).
 * The steps are as follows.
 * (1) RM tries to reserve all (actually up to 95% for NUMA#0 and
 *     98% for others)
 * (2) RM calculates the amount to trim for each node
 * (3) RM calls the following function to do the trim
 */
static int __ihk_smp_release_mem_partially(size_t ihk_mem, int numa_id)
{
	int ret = -1;
	struct chunk *mem_chunk;
	size_t size_left = ihk_mem;
	unsigned long va;
	struct rb_root tmp_chunks = RB_ROOT;

	pr_info("IHK-SMP: partial release size: %ld, numa_id: %d\n",
		ihk_mem, numa_id);

	list_for_each_entry(mem_chunk, &ihk_mem_free_chunks, chain) {
		__mem_chunk_insert(&tmp_chunks, mem_chunk);
	}

	/* Release the smallest */
	while (1) {
		struct rb_node *node;
		unsigned long min = (unsigned long)-1;
		size_t size_taken;

		mem_chunk = NULL;

		for (node = rb_first(&tmp_chunks); node; node = rb_next(node)) {
			struct chunk *q = container_of(node, struct chunk,
						       node);

			if (q->numa_id != numa_id)
				continue;

			if (q->size < min) {
				mem_chunk = q;
				min = mem_chunk->size;
			}
		}

		if (!mem_chunk)
			break;

		rb_erase(&mem_chunk->node, &tmp_chunks);

		/* Release the whole chunk */
		if (mem_chunk->size <= size_left) {
			list_del(&mem_chunk->chain);
			__ihk_smp_release_chunk(mem_chunk);
			size_left -= mem_chunk->size;
			pr_info("IHK-SMP: chunk 0x%lx - 0x%lx"
				" (len: %ld) @ NUMA node: %d is released\n",
				mem_chunk->addr,
				mem_chunk->addr + mem_chunk->size,
				mem_chunk->size, mem_chunk->numa_id);
			goto next_chunk;
		}

		dprintk("%s: size_left: %ld\n", __func__, size_left);
		pr_info("IHK-SMP: shrinking chunk 0x%lx - 0x%lx"
			" (len: %ld) @ NUMA node: %d...\n",
			mem_chunk->addr, mem_chunk->addr + mem_chunk->size,
			mem_chunk->size, mem_chunk->numa_id);


		/* Release from the top. Alignment is improved by
		 * by early-termination.
		 */
		va = (unsigned long)phys_to_virt(mem_chunk->addr);
		size_taken = 0;
		while (1) {
			int order;
			size_t order_size;
			struct page *page = virt_to_page(va);

			if (!PageCompound(page) || !PageHead(page)) {
				dprintk("IHK-SMP: WARNING: page is not compound or not head"
					", freeing single page\n");
				free_page(va);
				size_taken += PAGE_SIZE;
				size_left -= PAGE_SIZE;
				va += PAGE_SIZE;
				goto next_compound;
			}

			order = compound_order(page);
			order_size = (PAGE_SIZE << order);

			dprintk(KERN_INFO "%s: order_size: %ld, size_left: %ld\n",
			       __func__, order_size, size_left);

			/* Don't split compound pages */
			if (size_left < order_size) {
				pr_info("IHK-SMP: skip %ld bytes not to split compound pages, order_size: %ld\n",
					size_left, order_size);
				size_left = 0;
				goto next_compound;
			}

#define IHK_RESERVE_MEM_GRANULE (4UL << 20)

			/* Improve alignment */
			if (size_left < IHK_RESERVE_MEM_GRANULE &&
			    (va & (IHK_RESERVE_MEM_GRANULE - 1)) == 0) {
				pr_info("IHK-SMP: skip %ld bytes for better alignment\n",
					size_left);
				size_left = 0;
				goto next_compound;
			}

			free_pages(va, order);

			size_taken += order_size;
			size_left -= order_size;
			va += order_size;

next_compound:
			if (size_left <= 0) {
				mem_chunk->addr += size_taken;
				mem_chunk->size -= size_taken;
				pr_info("IHK-SMP: chunk is shrunk to 0x%lx - 0x%lx"
				       " (len: %ld, NUMA node: %d)\n",
				       mem_chunk->addr,
					mem_chunk->addr + mem_chunk->size,
				       mem_chunk->size, mem_chunk->numa_id);
				break;
			}
		}

next_chunk:
		if (size_left <= 0) {
			ret = 0;
			goto out;
		}
	}

out:

	return ret;
}

static int _smp_ihk_write_cpu_sys_file(int cpu_id, char *val)
{
	struct file* filp = NULL;
	loff_t pos = 0;
	int ret, err = 0;
	char path[256];

	sprintf(path, "/sys/devices/system/cpu/cpu%d/online", cpu_id);

	filp = filp_open(path, O_RDWR, 0);
	if (IS_ERR(filp)) {
		 err = PTR_ERR(filp);
		 printk("%s: error opening %s\n", __FUNCTION__, path);
		 return -1;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)
	ret = kernel_write(filp, val, 1, &pos);
#else
	ret = kernel_write(filp, val, 1, pos);
#endif
	if (ret != 1) {
		 filp_close(filp, NULL);
		 printk("%s: error writing %s\n", __FUNCTION__, path);
		 return -1;
	}

	filp_close(filp, NULL);
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
	int i;
	cpumask_t cpus_to_offline;
	struct ihk_cpu_req req;
	int *req_cpus = NULL;
	char req_string[REQ_STR_MAXLEN];

	if (copy_from_user(&req, (void *)arg, sizeof(req))) {
		printk("%s: error: copying request\n", __FUNCTION__);
		return -EFAULT;
	}

	if (req.num_cpus == 0) {
		printk("%s: invalid request length\n", __FUNCTION__);
		return -EINVAL;
	}

	req_cpus = kmalloc(sizeof(int) * req.num_cpus, GFP_KERNEL);
	if (!req_cpus) {
		pr_err("%s: error: allocating request cpus\n", __func__);
		return -EINVAL;
	}

	if (copy_from_user(req_cpus, req.cpus, sizeof(int) * req.num_cpus)) {
		pr_err("%s: error: copying request cpus\n", __func__);
		ret = -EFAULT;
		goto out;
	}

	req_string[0] = '\0';
	cpu_array2str(req_string, sizeof(req_string), req.num_cpus, req_cpus);

	memset(&cpus_to_offline, 0, sizeof(cpus_to_offline));

	for (i = 0; i < req.num_cpus; i++) {
		if (req_cpus[i] < 0 || req_cpus[i] >= nr_cpu_ids) {
			pr_info("%s: error: CPU %d is out of range\n",
				__func__, req_cpus[i]);

			ret = -EINVAL;
			goto out;
		}
		cpumask_set_cpu(req_cpus[i], &cpus_to_offline);
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
			ret = -EINVAL;
			goto out;
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
	kfree(req_cpus);
	return ret;
}

static int _smp_ihk_release_cpu(cpumask_t *cpus_to_online)
{
	int ret;
	int cpu;

	/* Collect cores to be onlined */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,0,0)
	for_each_cpu(cpu, cpus_to_online) {
#else
	for_each_cpu_mask(cpu, *cpus_to_online) {
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
			pr_err("IHK-SMP: error: CPU %d is online\n",
			       cpu);
			ret = -EINVAL;
			goto err;
		}

		if (ihk_smp_cpus[cpu].status != IHK_SMP_CPU_AVAILABLE) {
			pr_err("%s: error: CPU %d isn't reserved\n",
			       __func__, cpu);
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
	return ret;
}

static int smp_ihk_release_cpu(ihk_device_t ihk_dev, unsigned long arg)
{
	int ret;
	int i;
	cpumask_t cpus_to_online;
	struct ihk_cpu_req req;
	int *req_cpus = NULL;

	if (copy_from_user(&req, (void *)arg, sizeof(req))) {
		pr_err("%s: error: copying request\n", __func__);
		return -EFAULT;
	}

	if (req.num_cpus == 0) {
		pr_err("%s: invalid request length\n", __func__);
		return -EINVAL;
	}

	req_cpus = kmalloc(sizeof(int) * req.num_cpus, GFP_KERNEL);
	if (!req_cpus) {
		pr_err("%s: error: allocating request string\n", __func__);
		return -EINVAL;
	}

	if (copy_from_user(req_cpus, req.cpus, sizeof(int) * req.num_cpus)) {
		pr_err("%s: error: copying request string\n", __func__);
		ret = -EFAULT;
		goto out;
	}

	memset(&cpus_to_online, 0, sizeof(cpus_to_online));

	for (i = 0; i < req.num_cpus; i++) {
		if (req_cpus[i] < 0 || req_cpus[i] >= nr_cpu_ids) {
			pr_info("%s: error: CPU %d is out of range\n",
				__func__, req_cpus[i]);

			ret = -EINVAL;
			goto out;
		}
		cpumask_set_cpu(req_cpus[i], &cpus_to_online);
	}

	ret = _smp_ihk_release_cpu(&cpus_to_online);
	if (ret) {
		pr_err("%s: error: _smp_ihk_release_cpu returned %d\n",
		       __func__, ret);
		goto out;
	}

 out:
	kfree(req_cpus);
	return ret;
}

static int smp_ihk_get_num_cpus(ihk_device_t ihk_dev)
{
	int cpu;
	int num_cpus = 0;

	for (cpu = 0; cpu < SMP_MAX_CPUS; ++cpu) {
		if (ihk_smp_cpus[cpu].status != IHK_SMP_CPU_AVAILABLE)
			continue;

		num_cpus++;
	}

	return num_cpus;
}

static int smp_ihk_query_cpu(ihk_device_t ihk_dev, unsigned long arg)
{
	int ret;
	int idx;
	struct ihk_cpu_req req;
	struct ihk_cpu_req *res = (struct ihk_cpu_req *)arg;
	int cpu;
	int *res_cpus = NULL;

	if (copy_from_user(&req, (void *)arg, sizeof(req))) {
		pr_err("%s: error: copying request\n", __func__);
		ret = -EFAULT;
		goto out;
	}

	for (cpu = 0, idx = 0; cpu < SMP_MAX_CPUS; ++cpu) {
		if (ihk_smp_cpus[cpu].status != IHK_SMP_CPU_AVAILABLE)
			continue;
		idx++;
	}

	if (idx != req.num_cpus) {
		pr_err("%s: error: #cpu requested (%d) != actual (%d)\n",
		       __func__, req.num_cpus, idx);
		ret = -EINVAL;
		goto out;
	}

	if (!(res_cpus = kmalloc(sizeof(int) * req.num_cpus, GFP_KERNEL))) {
		pr_err("%s: error: allocating res_cpus\n", __func__);
		ret = -ENOMEM;
		goto out;
	}

	for (cpu = 0, idx = 0; cpu < SMP_MAX_CPUS; ++cpu) {
		if (ihk_smp_cpus[cpu].status != IHK_SMP_CPU_AVAILABLE)
			continue;

		res_cpus[idx] = cpu;
		idx++;
	}

	if (req.num_cpus > 0) {
		if (copy_to_user(req.cpus, res_cpus,
				 sizeof(int) * req.num_cpus)) {
			pr_err("%s: error: copying CPU array to user-space\n",
			       __func__);
			ret = -EFAULT;
			goto out;
		}
	}

	if (copy_to_user(&res->num_cpus, &req.num_cpus, sizeof(int))) {
		pr_err("%s: error: copying numer of CPUs  to user-space\n",
		       __func__);
		ret = -EFAULT;
		goto out;
	}

	ret = 0;
out:
	kfree(res_cpus);
	return 0;
}

static int smp_ihk_reserve_mem(ihk_device_t ihk_dev, unsigned long arg)
{
	size_t mem_size;
	int numa_id;
	int ret = 0, i;
	struct ihk_mem_req req;
	size_t *req_sizes = NULL;
	int *req_numa_ids = NULL;

	if (copy_from_user(&req, (void *)arg, sizeof(req))) {
		printk("%s: error: copying request\n", __FUNCTION__);
		return -EFAULT;
	}

	if (req.num_chunks == 0) {
		printk("%s: invalid request length\n", __FUNCTION__);
		return -EINVAL;
	}

	req_sizes = kmalloc(sizeof(size_t) * req.num_chunks, GFP_KERNEL);
	if (!req_sizes) {
		pr_err("%s: error: allocating request sizes\n", __func__);
		return -ENOMEM;
	}

	req_numa_ids = kmalloc(sizeof(int) * req.num_chunks, GFP_KERNEL);
	if (!req_numa_ids) {
		pr_err("%s: error: allocating request numa_ids\n", __func__);
		return -ENOMEM;
	}

	if (copy_from_user(req_sizes, req.sizes,
			sizeof(size_t) * req.num_chunks)) {
		pr_err("%s: error: copying request sizes\n", __func__);
		ret = -EFAULT;
		goto out;
	}

	if (copy_from_user(req_numa_ids, req.numa_ids,
			sizeof(int) * req.num_chunks)) {
		pr_err("%s: error: copying request numa_ids\n", __func__);
		ret = -EFAULT;
		goto out;
	}

	/* Check mem size */
	for (i = 0; i < req.num_chunks; i++) {
		mem_size = req_sizes[i];
		if (mem_size != IHK_SMP_MEM_ALL &&
				mem_size % IHK_RESERVE_MEM_GRANULE != 0) {
			pr_err("%s: error: mem_size must be in "
			       "multiples of %ld bytes\n",
			       __func__, IHK_RESERVE_MEM_GRANULE);
			ret = -EINVAL;
			break;
		}
	}

	if (ret != 0) {
		goto out;
	}

	/* Do the reservation */
	for (i = 0; i < req.num_chunks; i++) {
		mem_size = req_sizes[i];
		numa_id = req_numa_ids[i];

		ret = __ihk_smp_reserve_mem(mem_size, numa_id,
					    req.min_chunk_size,
					    req.max_size_ratio_all,
					    req.timeout);
		if (ret != 0) {
			printk("IHK-SMP: reserve_mem: error: reserving memory\n");
			break;
		}
	}

out:
	kfree(req_sizes);
	kfree(req_numa_ids);
	return ret;
}

static int smp_ihk_release_mem(ihk_device_t ihk_dev, unsigned long arg)
{
	int ret = 0, i, ret_internal;
	struct ihk_mem_req req;
	size_t *req_sizes = NULL;
	int *req_numa_ids = NULL;

	ret_internal = copy_from_user(&req, (void *)arg, sizeof(req));
	ARCHDRV_CHKANDJUMP(ret_internal != 0, "copy_from_user failed", -EFAULT);

	ARCHDRV_CHKANDJUMP(req.num_chunks < 0, "invalid request length",
			-EINVAL);

	req_sizes = kmalloc(sizeof(size_t) * req.num_chunks, GFP_KERNEL);
	ARCHDRV_CHKANDJUMP(req_sizes == NULL, "kmalloc failed", -EINVAL);

	req_numa_ids = kmalloc(sizeof(int) * req.num_chunks, GFP_KERNEL);
	ARCHDRV_CHKANDJUMP(req_numa_ids == NULL, "kmalloc failed", -EINVAL);

	ret_internal = copy_from_user(req_sizes, req.sizes,
			sizeof(size_t) * req.num_chunks);
	ARCHDRV_CHKANDJUMP(ret_internal != 0, "copy_from_user failed", -EFAULT);

	ret_internal = copy_from_user(req_numa_ids, req.numa_ids,
			sizeof(int) * req.num_chunks);
	ARCHDRV_CHKANDJUMP(ret_internal != 0, "copy_from_user failed", -EFAULT);

	/* Do release */
	for (i = 0; i < req.num_chunks; i++) {
		ret = __ihk_smp_release_mem(req_sizes[i],
					    req_numa_ids[i]);
		if (ret) {
			pr_err("%s: error: __ihk_smp_release_mem returned %d\n",
			       __func__, ret);
			goto fn_fail;
		}
	}

 fn_fail:
	kfree(req_sizes);
	kfree(req_numa_ids);
	return ret;
}

static int smp_ihk_release_mem_partially(ihk_device_t ihk_dev,
					 unsigned long arg)
{
	int ret, i;
	struct ihk_mem_req req;
	size_t *req_sizes = NULL;
	int *req_numa_ids = NULL;

	ret = copy_from_user(&req, (void *)arg, sizeof(req));
	if (ret) {
		pr_err("%s: copy_from_user struct ihk_mem_req\n", __func__);
		ret = -EFAULT;
		goto out;
	}

	if (req.num_chunks < 0) {
		pr_err("%s: invalid number of chunks (%d)\n",
		       __func__, req.num_chunks);
		ret = -EINVAL;
		goto out;
	}

	req_sizes = kmalloc(sizeof(size_t) * req.num_chunks, GFP_KERNEL);
	if (!req_sizes) {
		pr_err("%s: allocating req_sizes\n", __func__);
		ret = -ENOMEM;
		goto out;
	}

	req_numa_ids = kmalloc(sizeof(int) * req.num_chunks, GFP_KERNEL);
	if (!req_numa_ids) {
		pr_err("%s: allocating req_num_ids\n", __func__);
		ret = -ENOMEM;
		goto out;
	}

	ret = copy_from_user(req_sizes, req.sizes,
			     sizeof(size_t) * req.num_chunks);
	if (ret) {
		pr_err("%s: copy_from_user req_sizes\n", __func__);
		ret = -EFAULT;
		goto out;
	}

	ret = copy_from_user(req_numa_ids, req.numa_ids,
			     sizeof(int) * req.num_chunks);
	if (ret) {
		pr_err("%s: copy_from_user req_numa_ids\n", __func__);
		ret = -EFAULT;
		goto out;
	}

	/* Do release */
	for (i = 0; i < req.num_chunks; i++) {
		if (req_sizes[i] > 0) {
			ret = __ihk_smp_release_mem_partially(req_sizes[i],
							      req_numa_ids[i]);
			if (ret) {
				pr_err("%s: __ihk_smp_release_mem_partially returned %d\n",
				       __func__, ret);
				ret = -EINVAL;
				goto out;
			}
		}
	}

	ret = 0;
out:
	kfree(req_sizes);
	kfree(req_numa_ids);
	return ret;
}

static int smp_ihk_query_mem(ihk_device_t ihk_dev, unsigned long arg)
{
	int ret, num_chunks = 0, idx = 0;
	struct ihk_mem_req req;
	struct ihk_mem_req *res = (struct ihk_mem_req *)arg;
	struct chunk *mem_chunk;
	size_t *query_res_size = NULL;
	int *query_res_numa_id = NULL;

	if (copy_from_user(&req, (void *)arg, sizeof(req))) {
		pr_err("%s: error: copying request\n", __func__);
		return -EFAULT;
	}

	/* Count memory chunks */
	list_for_each_entry(mem_chunk, &ihk_mem_free_chunks, chain) {
		num_chunks++;
	}

	if (req.num_chunks == 0) {
		/* Get reserved mem chunks */
		if (copy_to_user(&res->num_chunks, &num_chunks, sizeof(int))) {
			pr_err("%s: error: copying mem num_chunks to user-space\n",
				__func__);
			ret = -EFAULT;
			goto out;
		}
		ret = 0;
		goto out;
	}

	if (!(query_res_size = kmalloc(sizeof(size_t) * num_chunks,
				GFP_KERNEL))) {
		pr_err("%s: error: allocating query_res_size\n",
			__func__);
		ret = -ENOMEM;
		goto out;
	}

	if (!(query_res_numa_id = kmalloc(sizeof(int) * num_chunks,
				GFP_KERNEL))) {
		pr_err("%s: error: allocating query_res_numa_id\n",
			__func__);
		ret = -ENOMEM;
		goto out;
	}

	/* Collect memory information */
	list_for_each_entry(mem_chunk, &ihk_mem_free_chunks, chain) {
		query_res_size[idx] = mem_chunk->size;
		query_res_numa_id[idx] = mem_chunk->numa_id;
		idx++;
	}

	if (idx > 0) {
		if (copy_to_user(req.sizes, query_res_size,
				sizeof(size_t) * idx)) {
			pr_err("%s: error: copying mem sizes to user-space\n",
				__func__);
			ret = -EFAULT;
			goto out;
		}
		if (copy_to_user(req.numa_ids, query_res_numa_id,
				sizeof(int) * idx)) {
			pr_err("%s: error: copying mem numa_ids to user-space\n",
				__func__);
			ret = -EFAULT;
			goto out;
		}
	}

	if (copy_to_user(&res->num_chunks, &idx, sizeof(int))) {
		pr_err("%s: error: copying mem num_chunks to user-space\n",
			__func__);
		ret = -EFAULT;
		goto out;
	}

	ret = 0;
out:
	kfree(query_res_size);
	kfree(query_res_numa_id);
	return ret;
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
	ssize_t ss;

	dprintk("read_file(%p,%ld,%s)\n", buf, size, fmt);
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
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)
	ss = kernel_read(fp, buf, size, &off);
#else
	ss = kernel_read(fp, off, buf, size);
#endif
	if (ss < 0) {
		error = ss;
		pr_warn("ihk:read_file:kernel_read failed. %d\n", error);
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
	dprintk("read_file(%p,%ld,%s): %d\n", buf, size, fmt, error);
	return error;
} /* read_file() */

int file_readable(char *fmt, ...)
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

#ifdef IHK_IKC_USE_LINUX_WORK_IRQ
/*
 * IHK IKC IRQ work function called from Linux IRQ work.
 */
void smp_ihk_ikc_irq_work_func(struct irq_work *work)
{
	smp_ihk_irq_call_handlers(0, NULL);
}
#endif // IHK_IKC_USE_LINUX_WORK_IRQ


static int smp_ihk_init(ihk_device_t ihk_dev, void *priv)
{
	int ret;
	int cpu = 0;

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

#if KERNEL_VERSION(4, 0, 0) <= LINUX_VERSION_CODE
	for_each_cpu(cpu, cpu_online_mask) {
#else
	for_each_cpu_mask(cpu, *cpu_online_mask) {
#endif
		ihk_smp_cpus[cpu].status = IHK_SMP_CPU_ONLINE;
	}

	ret = smp_ihk_arch_init();

	return ret;
}

static int smp_ihk_exit(ihk_device_t ihk_dev, void *priv)
{
	int cpu, ret = 0;

	smp_ihk_arch_exit();

	/* Re-enable CPU cores */
	for (cpu = 0; cpu < SMP_MAX_CPUS; ++cpu) {
		if ((ihk_smp_cpus[cpu].status == IHK_SMP_CPU_ONLINE) ||
		    (ihk_smp_cpus[cpu].status == IHK_SMP_CPU_NONE)) {
			continue;
		}

		ret = ihk_smp_reset_cpu(ihk_smp_cpus[cpu].hw_id);

		if (smp_ihk_online_cpu(cpu) != 0) {
			continue;
		}

		printk("IHK-SMP: CPU %d onlined successfully, HWID: %d\n",
		       ihk_smp_cpus[cpu].id, ihk_smp_cpus[cpu].hw_id);
	}

	/* Free memory */
	__smp_ihk_free_mem_from_list(&ihk_mem_free_chunks);

	free_info();

	return ret;
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
	.release_mem_partially = smp_ihk_release_mem_partially,
	.get_num_cpus = smp_ihk_get_num_cpus,
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

#ifdef IHK_IKC_USE_LINUX_WORK_IRQ
struct llist_head *ihk__raised_list;
#endif // IHK_IKC_USE_LINUX_WORK_IRQ

static int ihk_smp_symbols_init(void)
{
	int ret = -EFAULT;

	ihk_ioremap_page_range =
		(void *)kallsyms_lookup_name("ioremap_page_range");
	if (WARN_ON(!ihk_ioremap_page_range))
		goto err;

	ihk_vmap_area_lock = (void *)kallsyms_lookup_name("vmap_area_lock");
	if (WARN_ON(!ihk_vmap_area_lock))
		goto err;

	ihk_vmap_area_root = (void *)kallsyms_lookup_name("vmap_area_root");
	if (WARN_ON(!ihk_vmap_area_root))
		goto err;

	ihk___insert_vmap_area =
		(void *)kallsyms_lookup_name("__insert_vmap_area");
	if (WARN_ON(!ihk___insert_vmap_area))
		goto err;

	ihk___free_vmap_area = (void *)kallsyms_lookup_name("__free_vmap_area");
	if (WARN_ON(!ihk___free_vmap_area))
		goto err;

#ifdef IHK_IKC_USE_LINUX_WORK_IRQ
#ifndef CONFIG_IRQ_WORK
#error "error: can't use Linux work IRQ without Linux kernel CONFIG_IRQ_WORK"
#endif
	ihk__raised_list =
		(struct llist_head *)kallsyms_lookup_name("raised_list");
	if (WARN_ON(!ihk__raised_list))
		return -EFAULT;
#endif // IHK_IKC_USE_LINUX_WORK_IRQ

	smp_ihk_hstates = (struct hstate *)kallsyms_lookup_name("hstates");
	if (WARN_ON(!smp_ihk_hstates))
		goto err;

	smp_ihk_default_hstate_idx = (unsigned int *)
		kallsyms_lookup_name("default_hstate_idx");
	if (WARN_ON(!smp_ihk_default_hstate_idx))
		goto err;


	ret = 0;
err:
	return ret;
}

static int __init smp_module_init(void)
{
	ihk_device_t ihkd;
	int ret;

	printk(KERN_INFO "IHK-SMP: initializing...\n");

	if ((ret = ihk_smp_symbols_init())) {
		return ret;
	}

	if ((ret = ihk_smp_arch_symbols_init())) {
		return ret;
	}

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
	int i;
	cpumask_t cpus_to_online;
	struct chunk *mem_chunk;
	struct chunk *mem_chunk_next;

	printk(KERN_INFO "IHK-SMP: finalizing...\n");

	/* release reserved CPUs (available --> online) */
	memset(&cpus_to_online, 0, sizeof(cpus_to_online));

	for (i = 0; i < SMP_MAX_CPUS; ++i) {
		if (ihk_smp_cpus[i].status != IHK_SMP_CPU_AVAILABLE)
			continue;

		cpumask_set_cpu(i, &cpus_to_online);
	}

	_smp_ihk_release_cpu(&cpus_to_online);

	/* release reserved memory chunks */
	list_for_each_entry_safe(mem_chunk,
			mem_chunk_next, &ihk_mem_free_chunks, chain) {
		list_del(&mem_chunk->chain);
		__ihk_smp_release_chunk(mem_chunk);
		pr_info("IHK-SMP: chunk 0x%lx - 0x%lx"
			" (len: %lu) @ NUMA node: %d is released\n",
			mem_chunk->addr, mem_chunk->addr + mem_chunk->size,
			mem_chunk->size, mem_chunk->numa_id);
	}

	ihk_unregister_device(builtin_data.ihk_dev);
}

module_init(smp_module_init);
module_exit(smp_module_exit);

MODULE_LICENSE("Dual BSD/GPL");
