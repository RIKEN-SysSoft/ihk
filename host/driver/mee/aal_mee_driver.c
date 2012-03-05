/**
 * \file aal_mee_driver.c
 * \brief AAL Host Driver for MEE (Manycore Emulation Environemnt)
 *
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
#include <aal/aal_host_driver.h>
#include <aal/aal_host_misc.h>
#include <aal/aal_host_user.h>
#include <aal/misc/debug.h>
#include <ikc/msg.h>
#include <linux/shimos.h>
#include "mee_dma.h"

#ifndef CONFIG_SHIMOS
#error "SHIMOS is required to build MEE!"
#endif

#define MEE_OS_STATUS_INITIAL  0
#define MEE_OS_STATUS_LOADING  1
#define MEE_OS_STATUS_LOADED   2
#define MEE_OS_STATUS_BOOTING  3

#define MEE_MAX_CPUS 64

#define MEE_COM_VECTOR  0xf1

struct mee_boot_param {
	struct shimos_boot_param bp;

	unsigned long msg_buffer;
	unsigned long mikc_queue_recv, mikc_queue_send;

	unsigned long dma_address;
	unsigned long ident_table;

	char kernel_args[256];
};

struct mee_os_data {
	spinlock_t lock;

	struct mee_device_data *dev;
	unsigned long coremaps;
	unsigned long mem_start, mem_end;

	int boot_cpu;
	unsigned long boot_rip;

	struct aal_mem_info mem_info;
	struct aal_mem_region mem_region;
	struct aal_cpu_info cpu_info;
	int cpu_hw_ids[MEE_MAX_CPUS];

	char kernel_args[256]; /* will be copied to boot_param on boot */

	struct mee_boot_param param;

	int status;
};

#define MEE_DEV_STATUS_READY    0
#define MEE_DEV_STATUS_BOOTING  1

extern struct mee_dma_config_struct *mee_dma_config;
static unsigned long mee_dma_config_pa;

struct mee_device_data {
	spinlock_t lock;
	aal_device_t aal_dev;
	int status;

	struct aal_dma_channel mee_host_channel;
};

static int mee_dma_request(aal_dma_channel_t, struct aal_dma_request *);

struct aal_dma_ops mee_dma_ops = {
	.request = mee_dma_request,
};

static aal_dma_channel_t mee_aal_get_dma_channel(aal_device_t dev, void *priv,
                                                 int channel)
{
	struct mee_device_data *data = priv;

	data->mee_host_channel.dev = dev;
	data->mee_host_channel.channel = 0;
	data->mee_host_channel.ops = &mee_dma_ops;

	return &data->mee_host_channel;
}

static void set_os_status(struct mee_os_data *os, int status)
{
	unsigned long flags;

	spin_lock_irqsave(&os->lock, flags);
	os->status = status;
	spin_unlock_irqrestore(&os->lock, flags);
}

static void set_dev_status(struct mee_device_data *dev, int status)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->lock, flags);
	dev->status = status;
	spin_unlock_irqrestore(&dev->lock, flags);
}

static void __build_os_info(struct mee_os_data *os)
{
	int i, c;

	os->mem_info.n_mappable = os->mem_info.n_available = 1;
	os->mem_info.n_fixed = 0;
	os->mem_info.available = os->mem_info.mappable = &os->mem_region;
	os->mem_info.fixed = NULL;
	os->mem_region.start = os->mem_start;
	os->mem_region.size = os->mem_end - os->mem_start;
	
	for (i = 0, c = 0; i < MEE_MAX_CPUS; i++) {
		if (os->coremaps & (1ULL << i)) {
			os->cpu_hw_ids[c] = i;
			c++;
		}
	}
	os->cpu_info.n_cpus = c;
	os->cpu_info.hw_ids = os->cpu_hw_ids;
}

static int mee_aal_os_boot(aal_os_t aal_os, void *priv, int flag)
{
	struct mee_os_data *os = priv;
	struct mee_device_data *dev = os->dev;
	unsigned long flags;

	spin_lock_irqsave(&dev->lock, flags);
	if (dev->status != MEE_DEV_STATUS_READY) {
		spin_unlock_irqrestore(&dev->lock, flags);
		printk("mee: Device is busy booting another OS.\n");
		return -EINVAL;
	}
	dev->status = MEE_DEV_STATUS_BOOTING;
	spin_unlock_irqrestore(&dev->lock, flags);
	
	__build_os_info(os);
	if (os->cpu_info.n_cpus < 1) {
		dprintf("mee: There are no CPU to boot!\n");
		set_dev_status(dev, MEE_DEV_STATUS_READY);

		return -EINVAL;
	}
	os->boot_cpu = os->cpu_info.hw_ids[0];

	set_os_status(os, MEE_OS_STATUS_BOOTING);

	dprint_var_x4(os->boot_cpu);
	dprint_var_x8(os->boot_rip);

	memset(&os->param, 0, sizeof(os->param));
	os->param.bp.start = os->mem_start;
	os->param.bp.end = os->mem_end;
	os->param.bp.cores = os->coremaps;
	os->param.dma_address = mee_dma_config_pa;
	os->param.ident_table = __pa(shimos_get_ident_page_table());
	strncpy(os->param.kernel_args, os->kernel_args,
	        sizeof(os->param.kernel_args));

	dprintf("boot cpu : %d, %lx, %lx, %lx, %lx\n",
	        os->boot_cpu, os->mem_start, os->mem_end, os->coremaps,
	        mee_dma_config_pa);

	return shimos_boot_cpu_kloader(os->boot_cpu, os->boot_rip,
	                               &os->param.bp);
}

static int mee_aal_os_load_mem(aal_os_t aal_os, void *priv, const char *buf,
                               unsigned long size, long offset)
{
	struct mee_os_data *os = priv;
	unsigned long phys, to_read, flags;
	void *virt;

	dprint_func_enter;

	/* We just load from the lowest address of the private memory */
	if (!os->coremaps || os->mem_end - os->mem_start < 0) {
		printk("mee: OS is not ready to boot.\n");
		return -EINVAL;
	}
	if (os->mem_start + size > os->mem_end) {
		printk("mee: OS is too big to load.\n");
		return -E2BIG;
	}

	spin_lock_irqsave(&os->lock, flags);
	if (os->status != MEE_OS_STATUS_INITIAL) {
		printk("mee: OS status is not initial.\n");
		spin_unlock_irqrestore(&os->lock, flags);
		return -EBUSY;
	}
	os->status = MEE_OS_STATUS_LOADING;
	spin_unlock_irqrestore(&os->lock, flags);

	offset += os->mem_start;
	phys = (offset & PAGE_MASK);
	offset -= phys;

	for (; size > 0; ) {
		virt = ioremap_cache(phys, PAGE_SIZE);
		if (!virt) {
			dprintf("mee: Failed to map %lx\n", phys);

			set_os_status(os, MEE_OS_STATUS_INITIAL);

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
		iounmap(virt);

		phys += PAGE_SIZE;
	}

	os->boot_rip = os->mem_start;

	set_os_status(os, MEE_OS_STATUS_INITIAL);
	
	return 0;
}

static int mee_aal_os_shutdown(aal_os_t aal_os, void *priv, int flag)
{
	struct mee_os_data *os = priv;
	int i, apicid;
	unsigned long flags, st, ed;

	for (i = MEE_MAX_CPUS - 1; i >= 0; i--) {
		if (os->coremaps & (1ULL << i)) {
			shimos_reset_cpu(i);

			apicid = i;
			shimos_free_cpus(1, &apicid);
		}
	}

	spin_lock_irqsave(&os->lock, flags);
	os->coremaps = 0;
	st = os->mem_start;
	ed = os->mem_end;
	os->mem_start = os->mem_end = 0;
	os->status = MEE_OS_STATUS_INITIAL;
	spin_unlock_irqrestore(&os->lock, flags);

	shimos_free_memory(st, ed - st);

	return 0;
}

static int mee_aal_os_alloc_resource(aal_os_t aal_os, void *priv,
                                     struct aal_resource *resource)
{
	struct mee_os_data *os = priv;
	int apicids[MEE_MAX_CPUS];
	int i, n, ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&os->lock, flags);
	if (os->status != MEE_OS_STATUS_INITIAL) {
		spin_unlock_irqrestore(&os->lock, flags);
		return -EBUSY;
	}
	os->status = MEE_OS_STATUS_LOADING;
	spin_unlock_irqrestore(&os->lock, flags);

	if (resource->cpu_cores) {
		if (resource->cpu_cores > MEE_MAX_CPUS) {
			ret = -EINVAL;
		} else if (resource->flags & AAL_RESOURCE_FLAG_CPU_SPECIFIED) {
			n = resource->cpu_cores;
			if (shimos_reserve_cpus(resource->cpu_cores, 
			                        resource->cores) == 0) {
				for (i = 0; i < n; i++) {
					os->coremaps |= 
						(1ULL << resource->cores[i]);
				}
 			} else {
				ret = -ENOMEM;
			}
		} else {
			n = shimos_allocate_cpus(resource->cpu_cores, apicids);
			for (i = 0; i < n; i++) {
				if (apicids[i] < MEE_MAX_CPUS) {
					dprintf("MEE: Core %d allocated.\n",
					        apicids[i]);
					os->coremaps |= (1ULL << apicids[i]);
				}
			}
			if (n <= 0) {
				ret = -ENOMEM;
			}
		}
	}

	/* TODO: When we allocate more than an area... */
	if (!ret && resource->mem_size) {
		if (resource->flags & AAL_RESOURCE_FLAG_MEM_SPECIFIED) {
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

			dprintf("MEE: Memory %lx - %lx allocated.\n",
			        os->mem_start, os->mem_end);
		}
	}

	set_os_status(os, MEE_OS_STATUS_INITIAL);
	return ret;
}

static enum aal_os_status mee_aal_os_query_status(aal_os_t aal_os, void *priv)
{
	struct mee_os_data *os = priv;
	int status;

	status = os->status;

	if (status == MEE_OS_STATUS_BOOTING) {
		if (os->param.bp.status == 1) {
			return AAL_OS_STATUS_BOOTED;
		} else if(os->param.bp.status == 2) {
			return AAL_OS_STATUS_READY;
		} else {
			return AAL_OS_STATUS_BOOTING;
		}
	} else {
		return AAL_OS_STATUS_NOT_BOOTED;
	}
}

static int mee_aal_os_set_kargs(aal_os_t aal_os, void *priv, char *buf)
{
	unsigned long flags;
	struct mee_os_data *os = priv;

	spin_lock_irqsave(&os->lock, flags);
	if (os->status != MEE_OS_STATUS_INITIAL) {
		printk("mee: OS status is not initial.\n");
		spin_unlock_irqrestore(&os->lock, flags);
		return -EBUSY;
	}
	os->status = MEE_OS_STATUS_LOADING;
	spin_unlock_irqrestore(&os->lock, flags);

	strncpy(os->kernel_args, buf, sizeof(os->kernel_args));

	set_os_status(os, MEE_OS_STATUS_INITIAL);

	return 0;
}

static int mee_aal_os_wait_for_status(aal_os_t aal_os, void *priv,
                                      enum aal_os_status status, 
                                      int sleepable, int timeout)
{
	enum aal_os_status s;
	if (sleepable) {
		/* TODO: Enable notification of status change, and wait */
		return -1;
	} else {
		/* Polling */
		while ((s = mee_aal_os_query_status(aal_os, priv)),
		       s != status && s < AAL_OS_STATUS_SHUTDOWN 
		       && timeout > 0) {
			mdelay(100);
			timeout--;
		}
		return s == status ? 0 : -1;
	}
}

static int mee_aal_os_issue_interrupt(aal_os_t aal_os, void *priv,
                                      int cpu, int v)
{
	struct mee_os_data *os = priv;

	/* better calcuation or make map */
	if (cpu < 0 || cpu >= os->cpu_info.n_cpus) {
		return -EINVAL;
	}
	shimos_issue_ipi(os->cpu_info.hw_ids[cpu], v);

	return -EINVAL;
}

static unsigned long mee_aal_os_map_memory(aal_os_t aal_os, void *priv,
                                           unsigned long remote_phys,
                                           unsigned long size)
{
	/* We use the same physical memory. So no need to do something */
	return remote_phys;
}

static int mee_aal_os_unmap_memory(aal_os_t aal_os, void *priv,
                                    unsigned long local_phys,
                                    unsigned long size)
{
	return 0;
}

static int mee_aal_os_get_special_addr(aal_os_t aal_os, void *priv,
                                       enum aal_special_addr_type type,
                                       unsigned long *addr,
                                       unsigned long *size)
{
	struct mee_os_data *os = priv;

	switch (type) {
	case AAL_SPADDR_KMSG:
		if (os->param.msg_buffer) {
			*addr = os->param.msg_buffer;
			*size = 8192;
			return 0;
		}
		break;

	case AAL_SPADDR_MIKC_QUEUE_RECV:
		if (os->param.mikc_queue_recv) {
			*addr = os->param.mikc_queue_recv;
			*size = MASTER_IKCQ_SIZE;
			return 0;
		}
		break;
	case AAL_SPADDR_MIKC_QUEUE_SEND:
		if (os->param.mikc_queue_send) {
			*addr = os->param.mikc_queue_send;
			*size = MASTER_IKCQ_SIZE;
			return 0;
		}
		break;
	}

	return -EINVAL;
}

static long mee_aal_os_debug_request(aal_os_t aal_os, void *priv,
                                     unsigned int req, unsigned long arg)
{
	switch (req) {
	case AAL_OS_DEBUG_START:
		mee_aal_os_issue_interrupt(aal_os, priv, (arg >> 8),
		                           (arg & 0xff));
		return 0;
	}
	return -EINVAL;
}

static LIST_HEAD(mee_interrupt_handlers);

static int mee_aal_os_register_handler(aal_os_t os, void *os_priv, int itype,
                                       struct aal_host_interrupt_handler *h)
{
	h->os = os;
	h->os_priv = os_priv;
	list_add_tail(&h->list, &mee_interrupt_handlers);

	return 0;
}

static int mee_aal_os_unregister_handler(aal_os_t os, void *os_priv, int itype,
                                         struct aal_host_interrupt_handler *h)
{
	list_del(&h->list);
	return 0;
}

static irqreturn_t mee_irq_handler(int irq, void *data)
{
	struct aal_host_interrupt_handler *h;

	/* XXX: Linear search? */
	list_for_each_entry(h, &mee_interrupt_handlers, list) {
		if (h->func) {
			h->func(h->os, h->os_priv, h->priv);
		}
	}

	return IRQ_HANDLED;
}

static struct aal_mem_info *mee_aal_os_get_memory_info(aal_os_t aal_os,
                                                       void *priv)
{
	struct mee_os_data *os = priv;

	return &os->mem_info;
}

static struct aal_cpu_info *mee_aal_os_get_cpu_info(aal_os_t aal_os, void *priv)
{
	struct mee_os_data *os = priv;

	return &os->cpu_info;
}

static struct aal_os_ops mee_aal_os_ops = {
	.load_mem = mee_aal_os_load_mem,
	.boot = mee_aal_os_boot,
	.shutdown = mee_aal_os_shutdown,
	.alloc_resource = mee_aal_os_alloc_resource,
	.query_status = mee_aal_os_query_status,
	.wait_for_status = mee_aal_os_wait_for_status,
	.set_kargs = mee_aal_os_set_kargs,
	.issue_interrupt = mee_aal_os_issue_interrupt,
	.map_memory = mee_aal_os_map_memory,
	.unmap_memory = mee_aal_os_unmap_memory,
	.register_handler = mee_aal_os_register_handler,
	.unregister_handler = mee_aal_os_unregister_handler,
	.get_special_addr = mee_aal_os_get_special_addr,
	.debug_request = mee_aal_os_debug_request,
	.get_memory_info = mee_aal_os_get_memory_info,
	.get_cpu_info = mee_aal_os_get_cpu_info,
};	

static struct aal_register_os_data mee_os_reg_data = {
	.name = "meeos",
	.flag = 0,
	.ops = &mee_aal_os_ops,
};

static int mee_aal_create_os(aal_device_t aal_dev, void *priv,
                             unsigned long arg, aal_os_t aal_os,
                             struct aal_register_os_data *regdata)
{
	struct mee_device_data *data = priv;
	struct mee_os_data *os;

	*regdata = mee_os_reg_data;

	os = kzalloc(sizeof(struct mee_os_data), GFP_KERNEL);
	if (!os) {
		data->status = 0; /* No other one should reach here */
		return -ENOMEM;
	}
	spin_lock_init(&os->lock);
	os->dev = data;
	regdata->priv = os;

	return 0;
}

static unsigned long mee_aal_map_memory(aal_device_t aal_dev, void *priv,
                                        unsigned long remote_phys,
                                        unsigned long size)
{
	/* We use the same physical memory. So no need to do something */
	return remote_phys;
}

static int mee_aal_unmap_memory(aal_device_t aal_dev, void *priv,
                                unsigned long local_phys,
                                unsigned long size)
{
	return 0;
}

static void *mee_aal_map_virtual(aal_device_t aal_dev, void *priv,
                                 unsigned long phys, unsigned long size,
                                 void *virt, int flags)
{
	if (!virt) {
		return ioremap_cache(phys, size);
	} else {
		return aal_host_map_generic(aal_dev, phys, virt, size, flags);
	}
}

static int mee_aal_unmap_virtual(aal_device_t aal_dev, void *priv,
                                  void *virt, unsigned long size)
{
	if ((unsigned long)virt >= PAGE_OFFSET) {
		iounmap(virt);
		return 0;
	} else {
		return aal_host_unmap_generic(aal_dev, virt, size);
	}
	return 0;
}

static long mee_aal_debug_request(aal_device_t aal_dev, void *priv,
                                  unsigned int req, unsigned long arg)
{
	switch (req) {
	case AAL_DEVICE_DEBUG_START + 0x10:
		mee_dma_issue_interrupt();
		return 0;
	}
	return -EINVAL;
}

static struct aal_device_ops mee_aal_device_ops = {
	.create_os = mee_aal_create_os,
	.map_memory = mee_aal_map_memory,
	.unmap_memory = mee_aal_unmap_memory,
	.map_virtual = mee_aal_map_virtual,
	.unmap_virtual = mee_aal_unmap_virtual,
	.debug_request = mee_aal_debug_request,
	.get_dma_channel = mee_aal_get_dma_channel,
};	

/* Only one device instance available */
static struct mee_device_data mee_data;

static struct aal_register_device_data mee_dev_reg_data = {
	.name = "mee",
	.flag = 0,
	.priv = &mee_data,
	.ops = &mee_aal_device_ops,
};

static int mee_dma_init(void);
static void mee_dma_exit(void);

static int __init mee_init(void)
{
	aal_device_t aald;

	printk(KERN_INFO "mee: MEE initializing...\n");

	spin_lock_init(&mee_data.lock);

	if (!(aald = aal_register_device(&mee_dev_reg_data))) {
		printk(KERN_INFO "mee: Failed to register aal driver.\n");
		return -ENOMEM;
	}

	mee_data.aal_dev = aald;

	shimos_set_irq_handler(mee_irq_handler);

	mee_dma_init();

	return 0;
}

static void __exit mee_exit(void)
{
	printk(KERN_INFO "mee: MEE finalizing...\n");
	aal_unregister_device(mee_data.aal_dev);

	shimos_set_irq_handler(NULL);

	mee_dma_exit();
}

module_init(mee_init);
module_exit(mee_exit);

MODULE_LICENSE("Dual BSD/GPL");

/*
 * DMA stuff
 */
static int mee_dma_apicid = -1;
module_param(mee_dma_apicid, int, 0444);

static unsigned long *mee_dma_page_table;
static unsigned long mee_dma_stack[512] __attribute__((aligned(4096)));
static unsigned long mee_dma_pt_pa;

extern void *shimos_get_ident_page_table(void);

#define MEE_DMA_VECTOR 0xf2
static struct idt_entry{
	uint32_t desc[4];
} *dma_idt;

struct x86_desc_ptr {
        uint16_t size;
        uint64_t address;
} __attribute__((packed));

static struct x86_desc_ptr dma_idt_ptr;

extern char mee_dma_intr_enter[];

static void set_idt_entry(int idx, unsigned long addr)
{
	dma_idt[idx].desc[0] = (addr & 0xffff) | (__KERNEL_CS << 16);
	dma_idt[idx].desc[1] = (addr & 0xffff0000) | 0x8e00;
	dma_idt[idx].desc[2] = (addr >> 32);
	dma_idt[idx].desc[3] = 0;
}

static void __prepare_idt(void)
{
	dma_idt = (void *)__get_free_page(GFP_KERNEL);

	dma_idt_ptr.size = sizeof(struct idt_entry) * 256;
	dma_idt_ptr.address = (unsigned long)dma_idt;

	set_idt_entry(MEE_DMA_VECTOR, (unsigned long)mee_dma_intr_enter);
}

static void shimos_dma_start(void)
{
	unsigned long cr3;

	asm volatile("movq %%cr3, %0" : "=r"(cr3));

	/* Copy the ident area */
	memcpy(mee_dma_page_table,
	       shimos_get_ident_page_table(),
	       PAGE_SIZE);

	/* Copy the kernel area */
	memcpy(mee_dma_page_table + 256,
	       phys_to_virt(cr3 + (PAGE_SIZE >> 1)),
	       PAGE_SIZE >> 1);

	asm volatile("lidt %0" : : "m"(dma_idt_ptr));

	asm volatile("movq %0, %%cr3" : : "r"(mee_dma_pt_pa));
	asm volatile("movq %0, %%rsp\n"
	             "callq shimos_dma_main" : : "r"(mee_dma_stack + 512));
}

static int mee_dma_init(void)
{
	int apicid;

	if (mee_dma_apicid >= 0) {
		if (shimos_reserve_cpus(1, &mee_dma_apicid)) {
			printk("MEE: Failed to reserve CPU core for DMA!\n");
			return -ENOMEM;
		}
	} else { 
		if (shimos_allocate_cpus(1, &apicid) != 1) {
			printk("MEE: Failed to allocate CPU core for DMA!\n");
			return -ENOMEM;
		}
		mee_dma_apicid = apicid;
	}
	printk("MEE: DMA Core APIC ID = %d\n", mee_dma_apicid);

	/* XXX: module only */
	__prepare_idt();
	mee_dma_page_table = (void *)__get_free_page(GFP_KERNEL);
	mee_dma_pt_pa = virt_to_phys(mee_dma_page_table);
	printk("Page table : %p => %lx\n", mee_dma_page_table, mee_dma_pt_pa);

	mee_dma_config = kmalloc(sizeof(struct mee_dma_config_struct),
	                         GFP_KERNEL);
	mee_dma_config_pa = virt_to_phys(mee_dma_config);

	shimos_boot_cpu_linux(mee_dma_apicid, (unsigned long)shimos_dma_start);

	/* Wait for dma boot */

	while (!mee_dma_config->status) {
		mb();
		cpu_relax();
	}
	printk("DMA Start Acked : %ld\n", sizeof(struct aal_dma_request));

	mee_dma_desc_init();

	return 0;
}

static void mee_dma_exit(void)
{
	shimos_reset_cpu(mee_dma_apicid);
	shimos_free_cpus(1, &mee_dma_apicid);

	free_page((unsigned long)mee_dma_page_table);
	free_page((unsigned long)dma_idt);
}

void mee_dma_issue_interrupt(void)
{
	shimos_issue_ipi(mee_dma_apicid, MEE_DMA_VECTOR);
}

extern int __mee_dma_request(aal_device_t dev, int channel,
                             struct aal_dma_request *req);

static int mee_dma_request(aal_dma_channel_t channel, struct aal_dma_request *r)
{
	__mee_dma_request(channel->dev, channel->channel, r);

	return 0;
}
