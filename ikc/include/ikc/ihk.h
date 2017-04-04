/**
 * \file ikc/include/ikc/ihk.h
 * \brief IHK-IKC: IHK wrapper functions.
 *
 * Copyright (C) 2011-2012 Taku Shimosawa <shimosawa@is.s.u-tokyo.ac.jp>
 */
#ifndef HEADER_IHK_IKC_IHK_H
#define HEADER_IHK_IKC_IHK_H

/* Support for both manycore and host side */
#ifdef IHK_OS_MANYCORE
#define ihk_ikc_spinlock_lock    ihk_mc_spinlock_lock
#define ihk_ikc_spinlock_unlock  ihk_mc_spinlock_unlock
#define ihk_ikc_spinlock_init    ihk_mc_spinlock_init

#define ihk_ikc_map_memory       ihk_mc_map_memory
#define ihk_ikc_unmap_memory     ihk_mc_unmap_memory

#define ihk_ikc_map_virtual(dev, p, n, a)  ihk_mc_map_virtual(p, n, a)
#define ihk_ikc_unmap_virtual(dev, v, n)   ihk_mc_unmap_virtual(v, n, 1)

#define ihk_ikc_get_processor_id ihk_mc_get_processor_id
#define ihk_ikc_mb               ihk_mc_mb

#define ihk_os_to_dev(os)        NULL

typedef void * ihk_os_t;
typedef void * ihk_device_t;

typedef void * ihk_wait_t;

#define IHK_THIS_OS  ((ihk_os_t)-1L)

#include <types.h>
#include <list.h>
#include <ihk/debug.h>
#include <string.h>
#include <ihk/atomic.h>
#include <ihk/lock.h>
#include <ihk/mm.h>
#include <errno.h>

#define IHK_EXPORT_SYMBOL(x)

#else /* !IHK_OS_MANYCORE */

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
#include <ihk/ihk_host_driver.h>

#define IHK_EXPORT_SYMBOL        EXPORT_SYMBOL

#define ihk_spinlock_t           spinlock_t
#define ihk_ikc_spinlock_lock(lock) \
	({ unsigned long __flags; spin_lock_irqsave(lock, __flags); __flags; })
#define ihk_ikc_spinlock_unlock  spin_unlock_irqrestore
#define ihk_ikc_spinlock_init    spin_lock_init

#define ihk_atomic_t             atomic_t
#define ihk_atomic_inc_return    atomic_inc_return

#define ihk_ikc_map_memory       ihk_os_map_memory
#define ihk_ikc_unmap_memory     ihk_os_unmap_memory

#define ihk_ikc_map_virtual(d, p, z, f) ihk_device_map_virtual(d, p, z, NULL, f)
#define ihk_ikc_unmap_virtual     ihk_device_unmap_virtual

#define ihk_ikc_get_processor_id() cpu_physical_id(smp_processor_id())
#define ihk_ikc_mb                mb

#define kprintf                  printk

typedef wait_queue_head_t        ihk_wait_t;

ihk_device_t ihk_os_to_dev(ihk_os_t);

#define ihk_ikc_get_unique_channel_id ihk_os_get_unique_channel_id
#define ihk_ikc_get_channel_list_lock ihk_os_get_ikc_channel_lock
#define ihk_ikc_get_channel_list      ihk_os_get_ikc_channel_list

#define ihk_ikc_get_regular_channel   ihk_os_get_regular_channel
#define ihk_ikc_set_regular_channel   ihk_os_set_regular_channel
#endif

#include <ikc/queue.h>

struct ihk_ikc_queue_head;
struct ihk_ikc_channel_desc;
struct ihk_ikc_master_wait_struct;

int ihk_ikc_send_interrupt(struct ihk_ikc_channel_desc *c);

struct ihk_ikc_queue_head *ihk_ikc_alloc_queue(int qpages);
void ihk_ikc_free_queue(struct ihk_ikc_queue_head *q);

void *ihk_ikc_malloc(int size);
void ihk_ikc_free(void *);

int call_arch_master_packet_handler(void *os, struct ihk_ikc_channel_desc *c,
                                    void *__packet);

void ihk_ikc_wait_init(ihk_wait_t *wait);
int ihk_ikc_wait_master(struct ihk_ikc_master_wait_struct *wq);
void ihk_ikc_wake_master(struct ihk_ikc_master_wait_struct *wq);

struct ihk_ikc_channel_desc *ihk_ikc_get_master_channel(ihk_os_t os);
struct list_head *ihk_ikc_get_channel_list(ihk_os_t os);
ihk_spinlock_t *ihk_ikc_get_channel_list_lock(ihk_os_t ihk_os);

struct ihk_ikc_channel_desc *ihk_ikc_get_regular_channel(ihk_os_t os, int cpu);
void ihk_ikc_set_regular_channel(ihk_os_t os, struct ihk_ikc_channel_desc *c, int cpu);

int ihk_ikc_get_unique_channel_id(ihk_os_t ihk_os);
void ihk_ikc_notify_remote_read(struct ihk_ikc_channel_desc *c);
void ihk_ikc_notify_remote_write(struct ihk_ikc_channel_desc *c);

#endif
