/**
 * \file ihk_monitor.h
 * \brief
 *	IHK-Master: OS status
 * \author Tomoki Shirasawa  <tomoki.shirasawa.kk@hitachi-solutions.com> \par
 *	Copyright (C) 2017 Tomoki Shirasawa  <tomoki.shirasawa.kk@hitachi-solutions.com>
 */
#ifndef IHK_MONITOR_H_INCLUDED
#define IHK_MONITOR_H_INCLUDED

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
};

struct ihk_os_monitor {
	unsigned long num_processors;
	unsigned long reserve[128];
	struct ihk_os_cpu_monitor cpu[0]; /* clv[i].monitor = &cpu[i] */
};
#endif /* !defined(IHK_MONITOR_H_INCLUDED) */
