#ifndef __HEADER_GENERIC_MEMORY_H
#define __HEADER_GENERIC_MEMORY_H

#include <arch-memory.h>

#ifndef KERNEL_PHYS_OFFSET
#define KERNEL_PHYS_OFFSET 0
#endif

static unsigned long virt_to_phys(void *v)
{
	return (unsigned long)v - KERNEL_PHYS_OFFSET;
}
static void *phys_to_virt(unsigned long p)
{
	return (void *)(p + KERNEL_PHYS_OFFSET);
}


enum aal_mc_pt_attribute {
	PTATTR_WRITABLE   = 0x02,
	PTATTR_USER       = 0x04,
	PTATTR_LARGEPAGE  = 0x80,
	PTATTR_UNCACHABLE = 0x10000,
};

#endif

