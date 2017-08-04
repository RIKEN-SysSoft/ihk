/**
 * \file ihk_os_monitor.h
 * \brief
 *	IHK-Master: OS status
 * \author Tomoki Shirasawa  <tomoki.shirasawa.kk@hitachi-solutions.com> \par
 *	Copyright (C) 2017 Tomoki Shirasawa  <tomoki.shirasawa.kk@hitachi-solutions.com>
 */
#ifndef IHK_OS_MONITOR_H_INCLUDED
#define IHK_OS_MONITOR_H_INCLUDED

/** \brief IHK-Monitor */
struct ihk_os_cpu_monitor {
	int status;
#define IHK_OS_MONITOR_NOT_BOOT 0
#define IHK_OS_MONITOR_IDLE 1
#define IHK_OS_MONITOR_USER 2
#define IHK_OS_MONITOR_KERNEL 3
#define IHK_OS_MONITOR_KERNEL_HEAVY 4
#define IHK_OS_MONITOR_KERNEL_OFFLOAD 5
#define IHK_OS_MONITOR_KERNEL_FREEZING 8
#define IHK_OS_MONITOR_KERNEL_FROZEN 9
#define IHK_OS_MONITOR_KERNEL_THAW 10
#define IHK_OS_MONITOR_PANIC 99
	int status_bak;
	unsigned long counter;
	unsigned long ocounter;
	unsigned long user_tsc;
	unsigned long system_tsc;
};

#define IHK_MAX_NUM_PGSIZES 4
#define IHK_MAX_NUM_NUMA_NODES 1024
#define IHK_MAX_NUMA_CPUS 1024

struct ihk_os_monitor {
	long rusage_memory_stat_rss[IHK_MAX_NUM_PGSIZES];
	long rusage_memory_stat_mapped_file[IHK_MAX_NUM_PGSIZES];
	unsigned long rusage_memory_max_usage;
	unsigned long rusage_max_num_threads;
	unsigned long rusage_num_threads;
	long rusage_rss_current;
	unsigned long rusage_memory_kmem_usage;
	unsigned long rusage_memory_kmem_max_usage;
	unsigned long rusage_memory_numa_stat[IHK_MAX_NUM_NUMA_NODES];

	unsigned long rusage_total_memory;
	unsigned long rusage_total_memory_usage;
	unsigned long rusage_total_memory_max_usage;
	unsigned long num_processors;
	unsigned long ns_per_tsc;
	unsigned long reserve[128];

	struct ihk_os_cpu_monitor cpu[0];
};
#endif /* !defined(IHK_OS_MONITOR_H_INCLUDED) */
