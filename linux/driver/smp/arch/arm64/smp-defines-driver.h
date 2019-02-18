/* smp-defines-driver.h COPYRIGHT FUJITSU LIMITED 2015 */
/**
 * \file smp-defines-driver.h
 * \brief
 *	IHK SMP-ARM64 Driver: IHK Host Driver
 *                          for partitioning an AARCH64 SMP chip
 */
#ifndef HEADER_SMP_SMP_DEFINES_DRIVER_H
#define HEADER_SMP_SMP_DEFINES_DRIVER_H

/*
 *  arm64_memory_layout
 * ----+-----------+-----------------------
 *   # | page size | virtual memory space
 * ----+-----------+-----------------------
 *   1 |       4KB |  39bit [linux-linaro-tracking, upstream kernel]
 *   2 |       4KB |  48bit
 *   3 |      64KB |  42bit [CentOS]
 *   4 |      64KB |  48bit
 * ----+-----------+-----------------------
 */
#if defined(CONFIG_ARM64_4K_PAGES)
#
# if CONFIG_ARM64_VA_BITS == 39
#  define IHK_SMP_MEMORY_LAYOUT_TYPE 1
# elif CONFIG_ARM64_VA_BITS == 48
#  define IHK_SMP_MEMORY_LAYOUT_TYPE 2
# else
#   error invalid va bits
# endif
#
#elif defined(CONFIG_ARM64_16K_PAGES)
#
# error 16KB page size not supported.
#
#elif defined(CONFIG_ARM64_64K_PAGES)
#
# if CONFIG_ARM64_VA_BITS == 42
#  define IHK_SMP_MEMORY_LAYOUT_TYPE 3
# elif CONFIG_ARM64_VA_BITS == 48
#  define IHK_SMP_MEMORY_LAYOUT_TYPE 4
# else
#   error invalid va bits
# endif
#
#else
# error invalid page size
#endif

#if IHK_SMP_MEMORY_LAYOUT_TYPE == 1
# define IHK_SMP_LARGE_PAGE_SHIFT	21
# define IHK_SMP_MAP_KERNEL_START	0xffffffffff800000UL
#elif IHK_SMP_MEMORY_LAYOUT_TYPE == 2
# define IHK_SMP_LARGE_PAGE_SHIFT	21
# define IHK_SMP_MAP_KERNEL_START	0xffffffffff800000UL
#elif IHK_SMP_MEMORY_LAYOUT_TYPE == 3
# define IHK_SMP_LARGE_PAGE_SHIFT	16
# define IHK_SMP_MAP_KERNEL_START	0xffffffffe0000000UL
#elif IHK_SMP_MEMORY_LAYOUT_TYPE == 4
# define IHK_SMP_LARGE_PAGE_SHIFT	16
# define IHK_SMP_MAP_KERNEL_START	0xffffffffe0000000UL
#else
# error invalid memory layout type
#endif

#define IHK_SMP_CHUNK_BASE_SIZE	(4UL << 20)	/* 4MiB a chunk */

#define rdtsc() arch_counter_get_cntvct()

#endif /* HEADER_SMP_SMP_DEFINES_DRIVER_H */
