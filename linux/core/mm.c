/** 
 * \file mm.c
 * 
 * \brief IHK-Host: Memory management misc functions (not implemented yet)
 *
 * \author Taku Shimosawa  <shimosawa@is.s.u-tokyo.ac.jp> \par
 * Copyright (C) 2011 - 2012  Taku Shimosawa
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <asm/bitops.h>

#include <ihk/ihk_host_driver.h>

void *ihk_host_map_generic(ihk_device_t dev, unsigned long phys,
                           void *virt, unsigned long size, int flags)
{
	printk("%s: not implemented -> %lx, %p, %lx, %x\n",
	       __FUNCTION__, phys, virt, size, flags);

	return NULL;
}

int ihk_host_unmap_generic(ihk_device_t dev, void *virt, unsigned long size)
{
	printk("%s: not implemented -> %p, %lx\n", __FUNCTION__, virt, size);

	return -ENOSYS;
}

EXPORT_SYMBOL(ihk_host_map_generic);
EXPORT_SYMBOL(ihk_host_unmap_generic);
