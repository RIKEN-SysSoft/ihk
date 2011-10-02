#ifndef HEADER_AAL_IKC_AAL_H
#define HEADER_AAL_IKC_AAL_H

/* Support for both manycore and host side */
#ifdef AAL_OS_MANYCORE
#include <types.h>
#include <list.h>
#include <aal/ikc.h>
#include <aal/debug.h>
#include <string.h>
#include <memory.h>

#define aal_ikc_spinlock_lock    aal_mc_spinlock_lock
#define aal_ikc_spinlock_unlock  aal_mc_spinlock_unlock
#define aal_ikc_spinlock_init    aal_mc_spinlock_init
#else
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <aal/ikc.h>
#include <aal/aal_host_driver.h>

#define aal_ikc_spinlock_lock(lock) \
	({ unsigned long __flags; spin_lock_irqsave(lock, __flags); __flags; })
#define aal_ikc_spinlock_unlock  spin_unlock_irqrestore
#define aal_ikc_spinlock_init    spin_lock_init
#define kprintf                  printk

#endif

#endif
