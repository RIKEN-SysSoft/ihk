/**
 * \file ihk_host_misc.h
 * \brief
 *	 IHK-Host: Miscellaneous Functions
 * \author Taku Shimosawa  <shimosawa@is.s.u-tokyo.ac.jp> \par
 * Copyright (C) 2011-2012 Taku Shimosawa <shimosawa@is.s.u-tokyo.ac.jp>
 */
#ifndef IHK_HOST_MISC_H
#define IHK_HOST_MISC_H

#include <ihk/ihk_host_driver.h>

/* mem_alloc.c */
void *ihk_pagealloc_init(unsigned long start, unsigned long size,
                         unsigned long unit);
void ihk_pagealloc_destroy(void *__desc);
unsigned long ihk_pagealloc_alloc(void *__desc, int npages);
void ihk_pagealloc_free(void *__desc, unsigned long address, int npages);

unsigned long ihk_pagealloc_alloc_size(void *__desc, unsigned long size);
void ihk_pagealloc_free_size(void *__desc, unsigned long address,
                             unsigned long size);

/* mm.c */
void *ihk_host_map_generic(ihk_device_t dev, unsigned long phys,
                           void *virt, unsigned long size, int flags);
int ihk_host_unmap_generic(ihk_device_t dev, void *virt, unsigned long size);

#endif
