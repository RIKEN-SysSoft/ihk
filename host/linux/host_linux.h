/*
 * Accelerator Abstraction Layer Host Implementation for Linux
 */
#ifndef __HEADER_AAL_HOST_LINUX_H
#define __HEADER_AAL_HOST_LINUX_H

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

	void *kmsg_buf;
	unsigned long kmsg_pa, kmsg_len;

	struct list_head ikc_channels;
	struct aal_host_interrupt_handler ikc_handler;

	struct aal_ikc_channel_desc *mchannel;
};

#endif
