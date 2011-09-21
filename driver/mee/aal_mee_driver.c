/*
 * AAL MEE Driver
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
#include <aal/aal_host_driver.h>
#include <aal/aal_host_misc.h>
#include <aal/misc/debug.h>
#include <linux/shimos.h>

#ifndef CONFIG_SHIMOS
#error "SHIMOS is required to build MEE!"
#endif

#define MEE_OS_STATUS_INITIAL  0
#define MEE_OS_STATUS_LOADING  1
#define MEE_OS_STATUS_LOADED   2
#define MEE_OS_STATUS_BOOTING  3

#define MEE_MAX_CPUS 64

struct mee_os_data {
	spinlock_t lock;

	struct mee_device_data *dev;
	unsigned long coremaps;
	unsigned long mem_start, mem_end;

	int boot_cpu;
	unsigned long boot_rip;

	struct shimos_boot_param param;

	int status;
};

#define MEE_DEV_STATUS_READY    0
#define MEE_DEV_STATUS_BOOTING  1

struct mee_device_data {
	spinlock_t lock;
	aal_device_t aal_dev;
	int status;
};

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

static int mee_aal_os_boot(aal_os_t aal_os, void *priv, int flag)
{
	struct mee_os_data *os = priv;
	struct mee_device_data *dev = os->dev;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&dev->lock, flags);
	if (dev->status != MEE_DEV_STATUS_READY) {
		spin_unlock_irqrestore(&dev->lock, flags);
		printk("mee: Device is busy booting another OS.\n");
		return -EINVAL;
	}
	dev->status = MEE_DEV_STATUS_BOOTING;
	spin_unlock_irqrestore(&dev->lock, flags);
	
	for (i = 0; i < MEE_MAX_CPUS; i++) {
		if (os->coremaps & (1 << i)) {
			os->boot_cpu = i;
			break;
		}
	}
	if (i == MEE_MAX_CPUS) {
		printk("mee: There are no CPU to boot!\n");
		set_dev_status(dev, MEE_DEV_STATUS_READY);

		return -EINVAL;
	}

	set_os_status(os, MEE_OS_STATUS_BOOTING);

	dprint_var_x4(os->boot_cpu);
	dprint_var_x8(os->boot_rip);

	memset(&os->param, 0, sizeof(os->param));
	os->param.start = os->mem_start;
	os->param.end = os->mem_end;
	os->param.cores = os->coremaps;
	
	return shimos_boot_cpu_kloader(os->boot_cpu, os->boot_rip, &os->param);
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
			printk("mee: Failed to map %lx\n", phys);

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
	unsigned long start, flags;

	spin_lock_irqsave(&os->lock, flags);
	if (os->status != MEE_OS_STATUS_INITIAL) {
		spin_unlock_irqrestore(&os->lock, flags);
		return -EBUSY;
	}
	os->status = MEE_OS_STATUS_LOADING;
	spin_unlock_irqrestore(&os->lock, flags);

	if (resource->cores) {
		if (resource->cores > MEE_MAX_CPUS) {
			ret = -EINVAL;
		} else {
			n = shimos_allocate_cpus(resource->cores, apicids);
			for (i = 0; i < n; i++) {
				if (apicids[i] < MEE_MAX_CPUS) {
					dprintf("MEE: Core %d allocated.\n",
					        apicids[i]);
					os->coremaps |= (1 << apicids[i]);
				}
			}
			if (n <= 0) {
				ret = -ENOMEM;
			}
		}
	}
	/* TODO: When we allocate more than an area... */
	if (!ret && resource->memory) {
		if (shimos_allocate_memory(resource->memory,
		                           &start)) {
			ret = -ENOMEM;
		} else {
			os->mem_start = start;
			os->mem_end = os->mem_start + resource->memory;

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
		if (os->param.status == 1) {
			return AAL_OS_STATUS_BOOTED;
		} else if(os->param.status == 2) {
			return AAL_OS_STATUS_READY;
		} else {
			return AAL_OS_STATUS_BOOTING;
		}
	} else {
		return AAL_OS_STATUS_NOT_BOOTED;
	}
}

static int mee_aal_os_issue_interrupt(aal_os_t aal_os, void *priv,
                                      int cpu, int v)
{
	struct mee_os_data *os = priv;
	int i, c;

	/* better calcuation or make map */
	for (i = 0, c = 0; i < MEE_MAX_CPUS; i++) {
		if (os->coremaps & (1ULL << i)) {
			if (c == cpu) {
				shimos_issue_ipi(i, v);
				return 0;
			}
			c++;
		}
	}
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
		}
		return 0;
	}

	return -EINVAL;
}

static struct aal_os_ops mee_aal_os_ops = {
	.load_mem = mee_aal_os_load_mem,
	.boot = mee_aal_os_boot,
	.shutdown = mee_aal_os_shutdown,
	.alloc_resource = mee_aal_os_alloc_resource,
	.query_status = mee_aal_os_query_status,
	.issue_interrupt = mee_aal_os_issue_interrupt,
	.map_memory = mee_aal_os_map_memory,
	.unmap_memory = mee_aal_os_unmap_memory,
	.get_special_addr = mee_aal_os_get_special_addr,
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
                                         

static struct aal_device_ops mee_aal_device_ops = {
	.create_os = mee_aal_create_os,
	.map_virtual = mee_aal_map_virtual,
	.unmap_virtual = mee_aal_unmap_virtual,
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

	mee_dma_init();

	return 0;
}

static void __exit mee_exit(void)
{
	printk(KERN_INFO "mee: MEE finalizing...\n");
	aal_unregister_device(mee_data.aal_dev);
	mee_dma_exit();
}

module_init(mee_init);
module_exit(mee_exit);

MODULE_LICENSE("GPL");

/*
 * DMA stuff
 */
static int mee_dma_apicid;

static void shimos_dma_main(void)
{
	while (1){
		asm volatile("cli; hlt");
	}
}

static int mee_dma_init(void)
{
	int apicid;

	if (shimos_allocate_cpus(1, &apicid) != 1) {
		printk("MEE: Failed to allocate CPU core for DMA!\n");
		return -ENOMEM;
	}
	mee_dma_apicid = apicid;

	shimos_boot_cpu_linux(apicid, (unsigned long)shimos_dma_main);
	return 0;
}

static void mee_dma_exit(void)
{
	shimos_reset_cpu(mee_dma_apicid);
	shimos_free_cpus(1, &mee_dma_apicid);
}
