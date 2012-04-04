#ifndef HEADER_AAL_IKC_AAL_H
#define HEADER_AAL_IKC_AAL_H

/* Support for both manycore and host side */
#ifdef AAL_OS_MANYCORE
#define aal_ikc_spinlock_lock    aal_mc_spinlock_lock
#define aal_ikc_spinlock_unlock  aal_mc_spinlock_unlock
#define aal_ikc_spinlock_init    aal_mc_spinlock_init

#define aal_ikc_map_memory       aal_mc_map_memory
#define aal_ikc_unmap_memory     aal_mc_unmap_memory

#define aal_ikc_map_virtual(dev, p, n, a)  aal_mc_map_virtual(p, n, a)
#define aal_ikc_unmap_virtual(dev, v, n)   aal_mc_unmap_virtual(v, n)

#define aal_ikc_get_processor_id aal_mc_get_processor_id
#define aal_ikc_mb               aal_mc_mb

#define aal_os_to_dev(os)        NULL

typedef void * aal_os_t;
typedef void * aal_device_t;

typedef void * aal_wait_t;

#define AAL_THIS_OS  ((aal_os_t)-1L)

#include <types.h>
#include <list.h>
#include <aal/debug.h>
#include <string.h>
#include <aal/atomic.h>
#include <aal/lock.h>
#include <aal/mm.h>
#include <errno.h>

#define AAL_EXPORT_SYMBOL(x)

#else /* !AAL_OS_MANYCORE */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <asm/atomic.h>
#include <asm/errno.h>
#include <asm/io.h>
#include <aal/aal_host_driver.h>

#define AAL_EXPORT_SYMBOL        EXPORT_SYMBOL

#define aal_spinlock_t           spinlock_t
#define aal_ikc_spinlock_lock(lock) \
	({ unsigned long __flags; spin_lock_irqsave(lock, __flags); __flags; })
#define aal_ikc_spinlock_unlock  spin_unlock_irqrestore
#define aal_ikc_spinlock_init    spin_lock_init

#define aal_atomic_t             atomic_t
#define aal_atomic_inc_return    atomic_inc_return

#define aal_ikc_map_memory       aal_os_map_memory
#define aal_ikc_unmap_memory     aal_os_unmap_memory

#define aal_ikc_map_virtual(d, p, z, f) aal_device_map_virtual(d, p, z, NULL, f)
#define aal_ikc_unmap_virtual     aal_device_unmap_virtual

#define aal_ikc_get_processor_id() cpu_physical_id(smp_processor_id())
#define aal_ikc_mb                mb

#define kprintf                  printk

typedef wait_queue_head_t        aal_wait_t;

aal_device_t aal_os_to_dev(aal_os_t);

#define aal_ikc_get_unique_channel_id aal_os_get_unique_channel_id
#define aal_ikc_get_channel_list_lock aal_os_get_ikc_channel_lock
#define aal_ikc_get_channel_list      aal_os_get_ikc_channel_list

#endif

#include <ikc/queue.h>

struct aal_ikc_queue_head;
struct aal_ikc_channel_desc;
struct aal_ikc_master_wait_struct;

int aal_ikc_send_interrupt(struct aal_ikc_channel_desc *c);

struct aal_ikc_queue_head *aal_ikc_alloc_queue(int qpages);
void aal_ikc_free_queue(struct aal_ikc_queue_head *q);

void *aal_ikc_malloc(int size);
void aal_ikc_free(void *);

int call_arch_master_packet_handler(void *os, struct aal_ikc_channel_desc *c,
                                    void *__packet);

void aal_ikc_wait_init(aal_wait_t *wait);
int aal_ikc_wait_master(struct aal_ikc_master_wait_struct *wq);
void aal_ikc_wake_master(struct aal_ikc_master_wait_struct *wq);

struct aal_ikc_channel_desc *aal_ikc_get_master_channel(aal_os_t os);
struct list_head *aal_ikc_get_channel_list(aal_os_t os);
aal_spinlock_t *aal_ikc_get_channel_list_lock(aal_os_t aal_os);
int aal_ikc_get_unique_channel_id(aal_os_t aal_os);

#endif
