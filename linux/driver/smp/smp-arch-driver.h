/* smp-arch-driver.h COPYRIGHT FUJITSU LIMITED 2015-2016 */
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
#ifndef HEADER_SMP_SMP_ARCH_DRIVER_H
#define HEADER_SMP_SMP_ARCH_DRIVER_H

#include "smp-defines-driver.h"

#ifndef IHK_SMP_LARGE_PAGE_SHIFT
#	error "IHK_SMP_LARGE_PAGE_SHIFT undifined"
#endif
#define IHK_SMP_LARGE_PAGE	(1UL << IHK_SMP_LARGE_PAGE_SHIFT)
#define IHK_SMP_LARGE_PAGE_MASK	(~(IHK_SMP_LARGE_PAGE - 1))

#ifndef IHK_SMP_MAP_KERNEL_START
#	error "IHK_SMP_MAP_KERNEL_START undifined"
#endif

#ifndef IHK_SMP_CHUNK_BASE_SIZE
#	error "IHK_SMP_CHUNK_BASE_SIZE undifined"
#endif

int ihk_smp_get_hw_id(int cpu);
int smp_wakeup_secondary_cpu(int hw_id, unsigned long start_eip);
#ifdef POSTK_DEBUG_ARCH_DEP_29
unsigned long calc_ns_per_tsc(void);
#endif	/* POSTK_DEBUG_ARCH_DEP_29 */
void smp_ihk_setup_trampoline(void *priv);
unsigned long smp_ihk_adjust_entry(unsigned long entry,
                                          unsigned long phys);
int smp_ihk_os_setup_startup(void *priv, unsigned long entry,
                             unsigned long phys);
int smp_ihk_os_dump(ihk_os_t ihk_os, void *priv, dumpargs_t *args);
enum ihk_os_status smp_ihk_os_query_status(ihk_os_t ihk_os, void *priv);
int smp_ihk_os_issue_interrupt(ihk_os_t ihk_os, void *priv, int cpu, int v);
unsigned long smp_ihk_os_map_memory(ihk_os_t ihk_os, void *priv,
                                    unsigned long remote_phys,
                                    unsigned long size);
int smp_ihk_os_unmap_memory(ihk_os_t ihk_os, void *priv,
                            unsigned long local_phys,
                            unsigned long size);
unsigned long smp_ihk_map_memory(ihk_device_t ihk_dev, void *priv,
                                 unsigned long remote_phys,
                                 unsigned long size);
int smp_ihk_unmap_memory(ihk_device_t ihk_dev, void *priv,
                         unsigned long local_phys,
                         unsigned long size);
int smp_ihk_arch_init(void);
int ihk_smp_arch_symbols_init(void);
int ihk_smp_reset_cpu(int hw_id);
void smp_ihk_arch_exit(void);
int smp_ihk_os_unmap_lwk(void);
int smp_ihk_os_send_nmi(ihk_os_t ihk_os, void *priv, int mode);

#endif /* HEADER_SMP_SMP_ARCH_DRIVER_H */
