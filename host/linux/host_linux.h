/**
 * \file host_linux.h
 * \brief AAL-Host: Structures used in implementation of AAL-Host
 *
 * Copyright (c) 2011-2012 Taku Shimosawa <shimosawa@is.s.u-tokyo.ac.jp>
 */
/*
 * Accelerator Abstraction Layer Host Implementation for Linux
 */
#ifndef __HEADER_AAL_HOST_LINUX_H
#define __HEADER_AAL_HOST_LINUX_H

#include <ikc/master.h>

/** \brief Structure that manages a manycore device in Linux */
struct aal_host_linux_device_data {
	/** \brief Lock for this structure */
	spinlock_t lock;
	/** \brief Character device structure */
	struct cdev cdev;
	/** \brief Device number of the device file for this structure */
	dev_t dev_num;
	/** \brief Flag.
	 *
	 * AAL_DEVICE_FLAG_SHARABLE is set if the device file can be opened
	 * by multiple processes at the same time */
	int flag;
	/** \brief Minor number of the device file for this structure */
	int minor;
	/** \brief Name of this device (not the device file) */
	char *name;
	/** \brief Reference count of the device file */
	atomic_t refcount;
	/** \brief Function pointers of the AAL-Host handlers */
	struct aal_device_ops *ops;
	/** \brief Private pointer given by the AAL-Host device driver */
	void *priv;
};

/** \brief Structure that manages a kernel instance in Linux */
struct aal_host_linux_os_data {
	/** \brief Pointer to the device structure */
	struct aal_host_linux_device_data *dev_data;
	/** \brief Lock for this structure */
	spinlock_t lock;
	/** \brief Character device */
	struct cdev cdev;
	/** \brief Device number for the kernel file */
	dev_t dev_num;
	/** \brief Flag.
	 *
	 * AAL_OS_FLAG_SHARABLE is set if the kernel file can be opened
	 * by multiple processes at the same time */
	int flag;
	/** \brief Minor number of the kernel file */
	int minor;
	/** \brief Name of the kernel */
	char *name;
	/** \brief Reference count of the kernel file */
	atomic_t refcount;
	/** \brief Function pointers of the AAL-Host handlers */
	struct aal_os_ops *ops;
	/** \brief Private pointer given by the AAL-Host device driver */
	void *priv;

	/** \brief Mutex for the kernel message */
	struct mutex kmsg_mutex;
	/** \brief Host kernel virtual address to the kernel message buffer */
	void *kmsg_buf;
	/** \brief Host physical address to the kernel message buffer */
	unsigned long kmsg_pa;
	/** \brief Size of the kernel message buffer */
	unsigned long kmsg_len;

	/** \brief Flag whether the IKC is already initialized or not */
	int ikc_initialized;
	/** \brief Lock for the channel list */
	spinlock_t ikc_channel_lock;
	/** \brief List of the channels available */
	struct list_head ikc_channels;
	/** \brief Interrupt handler */
	struct aal_host_interrupt_handler ikc_handler;
	/** \brief Worker thread for the IKC interrupt handler */
	struct work_struct ikc_work;

	/** \brief IKC master channel between the host and this kernel */
	struct aal_ikc_channel_desc *mchannel;
	/** \brief Lock for listeners */
	spinlock_t listener_lock;
	/** \brief Array of the listeners */
	struct aal_ikc_listen_param *listeners[AAL_IKC_MAX_PORT];
	/** \brief Packet handler for the master channel */
	aal_ikc_ph_t packet_handler;
	/** \brief Last channel ID */
	atomic_t channel_id;

	/** \brief Lock for wait_list */
	spinlock_t wait_lock;
	/** \brief Wait list used in the IKC functions */
	struct list_head wait_list;

	/** \brief List of the additional ioctl handlers */
	struct list_head aux_call_list;
};

#endif
