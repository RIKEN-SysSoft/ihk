/**
 * \file ihk_builtin_driver.c
 * \brief
 *	IHK BUILTIN Driver: IHK Host Driver 
 *                        for BUILTIN (Manycore Emulation Environemnt)
 * \author Taku Shimosawa  <shimosawa@is.s.u-tokyo.ac.jp> \par
 * Copyright (C) 2011-2012 Taku SHIMOSAWA <shimosawa@is.s.u-tokyo.ac.jp>
 */
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/interrupt.h>
#include <linux/file.h>
#include <linux/elf.h>
#include <ihk/ihk_host_driver.h>
#include <ihk/ihk_host_misc.h>
#include <ihk/ihk_host_user.h>
#include <ihk/misc/debug.h>
#include <ikc/msg.h>
#include <linux/shimos.h>
#include "builtin_dma.h"

#ifndef CONFIG_SHIMOS
#error "SHIMOS is required to build BUILTIN!"
#endif

#define BUILTIN_OS_STATUS_INITIAL  0
#define BUILTIN_OS_STATUS_LOADING  1
#define BUILTIN_OS_STATUS_LOADED   2
#define BUILTIN_OS_STATUS_BOOTING  3

#define BUILTIN_MAX_CPUS SHIMOS_MAX_CORES

#define BUILTIN_COM_VECTOR  0xf1

#define LARGE_PAGE_SIZE	(1UL << 21)
#define LARGE_PAGE_MASK	(~((unsigned long)LARGE_PAGE_SIZE - 1))

#define MAP_ST_START	0xffff800000000000UL
#define MAP_KERNEL_START	0xffffffff80000000UL

#define PTL4_SHIFT	39
#define PTL3_SHIFT	30
#define PTL2_SHIFT	21

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

#ifdef USE_DMA
extern struct builtin_dma_config_struct *builtin_dma_config;
static unsigned long builtin_dma_config_pa;
#endif

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

#ifdef USE_DMA
static int builtin_dma_request(ihk_dma_channel_t, struct ihk_dma_request *);

struct ihk_dma_ops builtin_dma_ops = {
	.request = builtin_dma_request,
};
#endif

/** \brief Implementation of ihk_host_get_dma_channel.
 *
 * It returns the information of the only channel in the DMA emulating core. */
static ihk_dma_channel_t builtin_ihk_get_dma_channel(ihk_device_t dev, void *priv,
                                                 int channel)
{
#ifdef USE_DMA
	struct builtin_device_data *data = priv;

	data->builtin_host_channel.dev = dev;
	data->builtin_host_channel.channel = 0;
	data->builtin_host_channel.ops = &builtin_dma_ops;

	return &data->builtin_host_channel;
#else
	return NULL;
#endif
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

/** \brief Boot a kernel. */
static int builtin_ihk_os_boot(ihk_os_t ihk_os, void *priv, int flag)
{
	struct builtin_os_data *os = priv;
	struct builtin_device_data *dev = os->dev;
	unsigned long flags;

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
#ifdef USE_DMA
	os->param.dma_address = builtin_dma_config_pa;
#endif
	os->param.ident_table = __pa(shimos_get_ident_page_table());
	strncpy(os->param.kernel_args, os->kernel_args,
	        sizeof(os->param.kernel_args));

	dprintf("boot cpu : %d, %lx, %lx, %lx, %lx\n",
	        os->boot_cpu, os->mem_start, os->mem_end, os->coremaps[0],
	        os->param.dma_address
	);

	return shimos_boot_cpu_kloader(os->boot_cpu, os->boot_rip,
	                               &os->param.bp);
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
	elf64 = shimos_other_os_map(os->mem_end - PAGE_SIZE, PAGE_SIZE);
	fs = get_fs();
	set_fs(get_ds());
printk("read pa=%lx va=%lx\n", os->mem_end - PAGE_SIZE, (unsigned long)elf64);
	r = vfs_read(file, (char *)elf64, PAGE_SIZE, &pos);
	set_fs(fs);
	if (r <= 0) {
		printk("vfs_read failed: %ld\n", r);
		shimos_other_os_unmap(elf64, PAGE_SIZE);
		fput(file);
		return (int)r;
	}
	if(elf64->e_ident[0] != 0x7f ||
	   elf64->e_ident[1] != 'E' ||
	   elf64->e_ident[2] != 'L' ||
	   elf64->e_ident[3] != 'F' ||
	   elf64->e_phoff + sizeof(Elf64_Phdr) * elf64->e_phnum > PAGE_SIZE){
		printk("kernel: BAD ELF\n");
		shimos_other_os_unmap(elf64, PAGE_SIZE);
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
			buf = shimos_other_os_map(offset, PAGE_SIZE);
			fs = get_fs();
			set_fs(get_ds());
			r = vfs_read(file, buf, l, &pos);
			set_fs(fs);
			if(r != PAGE_SIZE){
				memset(buf + r, '\0', PAGE_SIZE - r);
			}
			shimos_other_os_unmap(buf, PAGE_SIZE);
			if (r <= 0) {
				printk("vfs_read failed: %ld\n", r);
				shimos_other_os_unmap(elf64, PAGE_SIZE);
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
			buf = shimos_other_os_map(offset, PAGE_SIZE);
			memset(buf, '\0', PAGE_SIZE);
			shimos_other_os_unmap(buf, PAGE_SIZE);
			offset += PAGE_SIZE;
		}
		if(offset > maxoffset)
			maxoffset = offset;
	}
	fput(file);
	shimos_other_os_unmap(elf64, PAGE_SIZE);

	pml4_p = os->mem_end - PAGE_SIZE;
	pdp_p = pml4_p - PAGE_SIZE;
	pde_p = pdp_p - PAGE_SIZE;


	cr3 = shimos_get_ident_page_table();
	pml4 = shimos_other_os_map(pml4_p, PAGE_SIZE);
	pdp = shimos_other_os_map(pdp_p, PAGE_SIZE);
	pde = shimos_other_os_map(pde_p, PAGE_SIZE);

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

	shimos_other_os_unmap(pde, PAGE_SIZE);
	shimos_other_os_unmap(pdp, PAGE_SIZE);
	shimos_other_os_unmap(pml4, PAGE_SIZE);

	startup_p = os->mem_end - (2 << PTL2_SHIFT);
	startup = shimos_other_os_map(startup_p, PAGE_SIZE);
	memcpy(startup, startup_data, startup_data_end - startup_data);
	startup[2] = pml4_p;
	startup[3] = 0xffffffffc0000000;
	startup[4] = phys;
	startup[5] = 0x10000;						/* trampoline page phys addr */
	startup[6] = (unsigned long)SHIMOS_VECTOR	/* IKC IRQ and core APIC id */
		| (0 << 32); 
	startup[7] = entry;
	shimos_other_os_unmap(startup, PAGE_SIZE);
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
		virt = shimos_other_os_map(phys, PAGE_SIZE);
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
		shimos_other_os_unmap(virt, PAGE_SIZE);

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
	unsigned long flags, st, ed;

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
		} else {
			n = shimos_allocate_cpus(resource->cpu_cores, apicids);
			for (i = 0; i < n; i++) {
				if (apicids[i] < BUILTIN_MAX_CPUS) {
					dprintf("BUILTIN: Core %d allocated.\n",
					        apicids[i]);
					CORE_SET(apicids[i], os->coremaps);
				}
			}
			if (n <= 0) {
				ret = -ENOMEM;
			}
		}
	}

	/* TODO: When we allocate more than an area... */
	if (!ret && resource->mem_size) {
		if (resource->flags & IHK_RESOURCE_FLAG_MEM_SPECIFIED) {
			if (shimos_reserve_memory(resource->mem_start,
			                          resource->mem_size)) {
				ret = -ENOMEM;
			}
		} else if (shimos_allocate_memory(resource->mem_size,
		                                  &resource->mem_start)) {
			ret = -ENOMEM;
		}

		if (!ret) { /* If successfully allocated ... */
			os->mem_start = resource->mem_start;
			os->mem_end = os->mem_start + resource->mem_size;

			dprintf("BUILTIN: Memory %lx - %lx allocated.\n",
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

static int builtin_ihk_os_dump(ihk_os_t ihk_os, void *priv, dumpargs_t *args)
{
	struct builtin_os_data *os = priv;

	if (0) printk("mcosdump: cmd %d start %lx size %lx buf %p\n",
			args->cmd, args->start, args->size, args->buf);

	if (args->cmd == DUMP_NMI) {
		int i;

		for (i = 0; i < os->cpu_info.n_cpus; ++i) {
#define NMI_MODE_NMI_VECTOR 0x402
			shimos_issue_ipi(os->cpu_info.hw_ids[i],
					NMI_MODE_NMI_VECTOR);
		}
		return 0;
	}

	if (args->cmd == DUMP_QUERY) {
		args->start = os->mem_start;
		args->size = os->mem_end - os->mem_start;
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

	return -EINVAL;
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
	shimos_issue_ipi(os->cpu_info.hw_ids[cpu], v);

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
	.dump = builtin_ihk_os_dump,
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
		return shimos_other_os_map(phys, size);
	} else {
		return ihk_host_map_generic(ihk_dev, phys, virt, size, flags);
	}
}

static int builtin_ihk_unmap_virtual(ihk_device_t ihk_dev, void *priv,
                                  void *virt, unsigned long size)
{
	if ((unsigned long)virt >= PAGE_OFFSET) {
		return shimos_other_os_unmap(virt, size);
	} else {
		return ihk_host_unmap_generic(ihk_dev, virt, size);
	}
	return 0;
}

static long builtin_ihk_debug_request(ihk_device_t ihk_dev, void *priv,
                                  unsigned int req, unsigned long arg)
{
#ifdef USE_DMA
	switch (req) {
	case IHK_DEVICE_DEBUG_START + 0x10:
		builtin_dma_issue_interrupt();
		return 0;
	}
#endif
	return -EINVAL;
}

static struct ihk_device_ops builtin_ihk_device_ops = {
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

#ifdef USE_DMA
static int builtin_dma_init(void);
static void builtin_dma_exit(void);
#endif

static int __init builtin_init(void)
{
	ihk_device_t ihkd;

	printk(KERN_INFO "builtin: BUILTIN initializing...\n");

	spin_lock_init(&builtin_data.lock);

	if (!(ihkd = ihk_register_device(&builtin_dev_reg_data))) {
		printk(KERN_INFO "builtin: Failed to register ihk driver.\n");
		return -ENOMEM;
	}

	builtin_data.ihk_dev = ihkd;

	shimos_set_irq_handler(builtin_irq_handler);

#ifdef USE_DMA
	builtin_dma_init();
#endif

	return 0;
}

static void __exit builtin_exit(void)
{
	printk(KERN_INFO "builtin: BUILTIN finalizing...\n");
	ihk_unregister_device(builtin_data.ihk_dev);

	shimos_set_irq_handler(NULL);

#ifdef USE_DMA
	builtin_dma_exit();
#endif
}

module_init(builtin_init);
module_exit(builtin_exit);

MODULE_LICENSE("Dual BSD/GPL");

#ifdef USE_DMA
/*
 * DMA stuff
 */
/** \brief APIC ID of the CPU core to use as a DMA-emulating core.
 * (Module parameter) */
static int dma_apicid = -1;
module_param(dma_apicid, int, 0444);

static int dmacpus = 1;
module_param(dmacpus, int, 0444);

/** \brief DMA page table used by the DMA core
 *
 * This page table contains the kernel mapping and identity mapping */
static unsigned long *builtin_dma_page_table;
/** \brief Physical address of builtin_dma_page_table */
static unsigned long builtin_dma_pt_pa;
/** \brief Stack used by the DMA core */
static unsigned long builtin_dma_stack[512] __attribute__((aligned(4096)));

extern void *shimos_get_ident_page_table(void);

/** \brief Vector number that triggers action of the DMA core */
#define BUILTIN_DMA_VECTOR 0xf2
static struct idt_entry{
	uint32_t desc[4];
} *dma_idt;

struct x86_desc_ptr {
        uint16_t size;
        uint64_t address;
} __attribute__((packed));

/** \brief IDT used by the DMA core */
static struct x86_desc_ptr dma_idt_ptr;

extern char builtin_dma_intr_enter[];

/** \brief Set an entry in IDT. */
static void set_idt_entry(int idx, unsigned long addr)
{
	dma_idt[idx].desc[0] = (addr & 0xffff) | (__KERNEL_CS << 16);
	dma_idt[idx].desc[1] = (addr & 0xffff0000) | 0x8e00;
	dma_idt[idx].desc[2] = (addr >> 32);
	dma_idt[idx].desc[3] = 0;
}

/** \brief Prepare an IDT that will be used by the DMA core. */
static void __prepare_idt(void)
{
	dma_idt = (void *)__get_free_page(GFP_KERNEL);

	dma_idt_ptr.size = sizeof(struct idt_entry) * 256;
	dma_idt_ptr.address = (unsigned long)dma_idt;

	set_idt_entry(BUILTIN_DMA_VECTOR, (unsigned long)builtin_dma_intr_enter);
}

/** \brief Initialization function that is executed by the DMA core
 * after head_64.S */
static void shimos_dma_start(void)
{
	unsigned long cr3;

	asm volatile("movq %%cr3, %0" : "=r"(cr3));

	/* Copy the ident area */
	memcpy(builtin_dma_page_table,
	       shimos_get_ident_page_table(),
	       PAGE_SIZE);

	/* Copy the kernel area */
	memcpy(builtin_dma_page_table + 256,
	       phys_to_virt(cr3 + (PAGE_SIZE >> 1)),
	       PAGE_SIZE >> 1);

	asm volatile("lidt %0" : : "m"(dma_idt_ptr));

	asm volatile("movq %0, %%cr3" : : "r"(builtin_dma_pt_pa));
	asm volatile("movq %0, %%rsp\n"
	             "callq shimos_dma_main" : : "r"(builtin_dma_stack + 512));
}

static int dma_dummycpu_size = 0;
static int dma_dummycpus[8];

/** \brief Initialization function of the DMA core
 *
 * This function allocates a DMA core, and prepares necessary tables
 * (interrupt tables and memory tables), then boot the DMA core,
 * and finally waits of the DMA core to boot.
 */
static int builtin_dma_init(void)
{
	int apicid;

	if (dma_apicid >= 0) {
		if (shimos_reserve_cpus(1, &dma_apicid)) {
			printk("BUILTIN: Failed to reserve CPU core for DMA!\n");
			return -ENOMEM;
		}
	} else { 
		if (shimos_allocate_cpus(1, &apicid) != 1) {
			printk("BUILTIN: Failed to allocate CPU core for DMA!\n");
			return -ENOMEM;
		}
		dma_apicid = apicid;
	}
	if(dma_apicid > 1){
		int i;

		if(dmacpus > 8)
			dmacpus = 8;
		dma_dummycpu_size = dmacpus - 1;
		for(i = 0; i < dmacpus; i++){
			int	cpu = (dma_apicid / dmacpus) * dmacpus + i;
			if(cpu == dma_apicid)
				continue;
			if(shimos_reserve_cpus(1, &cpu) == 0){
				dma_dummycpus[dma_dummycpu_size] = cpu;
				dma_dummycpu_size++;
				printk("DUMMY APIC=%d\n", cpu);
			}
		}
	}

	printk("BUILTIN: DMA Core APIC ID = %d\n", dma_apicid);

	/* XXX: module only */
	__prepare_idt();
	builtin_dma_page_table = (void *)__get_free_page(GFP_KERNEL);
	builtin_dma_pt_pa = virt_to_phys(builtin_dma_page_table);
	printk("Page table : %p => %lx\n", builtin_dma_page_table, builtin_dma_pt_pa);

	builtin_dma_config = kmalloc(sizeof(struct builtin_dma_config_struct),
	                         GFP_KERNEL);
	builtin_dma_config_pa = virt_to_phys(builtin_dma_config);

	shimos_boot_cpu_linux(dma_apicid, (unsigned long)shimos_dma_start);

	/* Wait for dma boot */

	while (!builtin_dma_config->status) {
		mb();
		cpu_relax();
	}
	printk("DMA Start Acked : %ld\n", sizeof(struct ihk_dma_request));

	builtin_dma_desc_init();

	return 0;
}

/** \brief Finalization function of the DMA core.
 *
 * This function resets the DMA core and frees pages. */
static void builtin_dma_exit(void)
{
	shimos_reset_cpu(dma_apicid);
	shimos_free_cpus(1, &dma_apicid);

/**
	for(i = 0; i < dma_dummycpu_size; i++){
		shimos_reset_cpu(dma_dummycpus[i]);
	}
	shimos_free_cpus(dma_dummycpu_size, dma_dummycpus);
	dma_dummycpu_size = 0;
*/

	free_page((unsigned long)builtin_dma_page_table);
	free_page((unsigned long)dma_idt);
}

/** \brief Issues an interrupt to the DMA core. */
void builtin_dma_issue_interrupt(void)
{
	shimos_issue_ipi(dma_apicid, BUILTIN_DMA_VECTOR);
}

extern int __builtin_dma_request(ihk_device_t dev, int channel,
                             struct ihk_dma_request *req);
/** \brief Wrapper function for __builtin_dma_request. */
static int builtin_dma_request(ihk_dma_channel_t channel, struct ihk_dma_request *r)
{
	__builtin_dma_request(channel->dev, channel->channel, r);

	return 0;
}
#endif
