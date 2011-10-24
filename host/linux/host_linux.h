/*
 * Accelerator Abstraction Layer Host Implementation for Linux
 */
#ifndef __HEADER_AAL_HOST_LINUX_H
#define __HEADER_AAL_HOST_LINUX_H

#include <ikc/master.h>

struct aal_host_linux_device_data {
	spinlock_t lock;
	struct cdev cdev;
	dev_t dev_num;
	int flag;
	int minor;
	char *name;
	atomic_t refcount;
	struct aal_device_ops *ops;
	void *priv;
};

struct aal_host_linux_os_data {
	struct aal_host_linux_device_data *dev_data;
	spinlock_t lock;
	struct cdev cdev;
	dev_t dev_num;
	int flag;
	int minor;
	char *name;
	atomic_t refcount;
	struct aal_os_ops *ops;
	void *priv;

	struct mutex kmsg_mutex;
	void *kmsg_buf;
	unsigned long kmsg_pa, kmsg_len;

	int ikc_initialized;
	spinlock_t ikc_channel_lock;
	struct list_head ikc_channels;
	struct aal_host_interrupt_handler ikc_handler;

	struct aal_ikc_channel_desc *mchannel;
	spinlock_t listener_lock;
	struct aal_ikc_listen_param *listeners[AAL_IKC_MAX_PORT];
	aal_ikc_ph_t packet_handler;
	atomic_t channel_id;

	spinlock_t wait_lock;
	struct list_head wait_list;
};

#endif
