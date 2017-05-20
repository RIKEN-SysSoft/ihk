/**
 * \file host_linux.h
 * 
 * \brief IHK-Host: Structures used in implementation of IHK-Host
 * Accelerator Abstraction Layer Host Implementation for Linux
 *
 * \author Taku Shimosawa  <shimosawa@is.s.u-tokyo.ac.jp> \par
 * Copyright (C) 2011 - 2012  Taku Shimosawa
 */
#ifndef __HEADER_IHK_HOST_LINUX_H
#define __HEADER_IHK_HOST_LINUX_H

#include <ikc/master.h>

/** \brief Structure that manages a manycore device in Linux */
struct ihk_host_linux_device_data {
	/** \brief Lock for this structure */
	spinlock_t lock;
	/** \brief Character device structure */
	struct cdev cdev;
	/** \brief Device number of the device file for this structure */
	dev_t dev_num;
	/** \brief Flag.
	 *
	 * IHK_DEVICE_FLAG_SHARABLE is set if the device file can be opened
	 * by multiple processes at the same time */
	int flag;
	/** \brief Minor number of the device file for this structure */
	int minor;
	/** \brief Name of this device (not the device file) */
	char *name;
	/** \brief Reference count of the device file */
	atomic_t refcount;
	/** \brief Function pointers of the IHK-Host handlers */
	struct ihk_device_ops *ops;
	/** \brief Private pointer given by the IHK-Host device driver */
	void *priv;
};

/** \brief Structure that manages a kernel instance in Linux */
struct ihk_host_linux_os_data {
	/** \brief Pointer to the device structure */
	struct ihk_host_linux_device_data *dev_data;
	/** \brief Lock for this structure */
	spinlock_t lock;
	/** \brief Character device */
	struct cdev cdev;
	/** \brief Device number for the kernel file */
	dev_t dev_num;
	/** \brief Flag.
	 *
	 * IHK_OS_FLAG_SHARABLE is set if the kernel file can be opened
	 * by multiple processes at the same time */
	int flag;
	/** \brief Minor number of the kernel file */
	int minor;
	/** \brief Name of the kernel */
	char *name;
	/** \brief Reference count of the kernel file */
	atomic_t refcount;
	/** \brief Function pointers of the IHK-Host handlers */
	struct ihk_os_ops *ops;
	/** \brief Private pointer given by the IHK-Host device driver */
	void *priv;

	/** \brief Mutex for the kernel message */
	struct mutex kmsg_mutex;
	/** \brief Host kernel virtual address to the kernel message buffer */
	void *kmsg_buf;
	/** \brief Host physical address to the kernel message buffer */
	unsigned long kmsg_pa;
	/** \brief Size of the kernel message buffer */
	unsigned long kmsg_len;

	/** \brief monitor */
	struct ihk_os_monitor *monitor;
	/** \brief Size of the monitor */
	unsigned long monitor_len;
	/** \brief Host physical address to monitor  */
	unsigned long monitor_pa;

	/** \brief Flag whether the IKC is already initialized or not */
	int ikc_initialized;
	/** \brief Lock for the channel list */
	spinlock_t ikc_channel_lock;
	/** \brief List of the channels available */
	struct list_head ikc_channels;

	/** \brief Interrupt handler */
	struct ihk_host_interrupt_handler ikc_handler;
	/** \brief Worker thread for the IKC interrupt handler */
	struct work_struct ikc_work;

	/** \brief IKC master channel between the host and this kernel */
	struct ihk_ikc_channel_desc *mchannel;
	/** \brief IKC regular channels between the host and this kernel */
	struct ihk_ikc_channel_desc **regular_channels;
	/** \brief Lock for listeners */
	spinlock_t listener_lock;
	/** \brief Array of the listeners */
	struct ihk_ikc_listen_param *listeners[IHK_IKC_MAX_PORT];
	/** \brief Packet handler for the master channel */
	ihk_ikc_ph_t packet_handler;
	/** \brief Last channel ID */
	atomic_t channel_id;

	/** \brief Lock for wait_list */
	spinlock_t wait_lock;
	/** \brief Wait list used in the IKC functions */
	struct list_head wait_list;

	/** \brief List of the additional ioctl handlers */
	struct list_head aux_call_list;

	/** \brief user data */
	void *usrdata;

	/** \brief linux struct device for /dev/mcos* */
	struct device *lindev;

	/** \brief lock for event list */
	spinlock_t event_list_lock;
	/** \brief event list */
	struct list_head event_list;

	/** \brief Linux kernel level callbacks */
	struct ihk_os_kernel_call_handler *kernel_handlers;
};

/** \brief Structure that manages a kernel instance fd in Linux */
struct ihk_file {
	/** \brief kernel instance */
	struct ihk_host_linux_os_data *osdata;
	/** \brief release handler */
	void (*release_handler)(ihk_os_t osdata, void *param);
	/** \brief param for handler */
	void *param;
	/** \brief mcos private data */
	void *mcos_data;
};

#endif
