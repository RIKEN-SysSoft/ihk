#ifndef HEADER_AAL_HOST_IKC_H
#define HEADER_AAL_HOST_IKC_H

#include <aal/aal_host_driver.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <ikc/queue.h>

struct aal_ikc_queue_head;

struct aal_ikc_queue_desc {
	struct aal_ikc_queue_head *queue;  /* Virtual address */
	spinlock_t                 lock;
	uint32_t                   intr_cpu;
	unsigned long              pa; /* If zero, queue is local */
	unsigned long              size;
};

struct aal_ikc_channel_desc {
	struct list_head           list;
	aal_os_t                   remote_os;
	int                        remote_id;
	int                        channel_id;
	struct aal_ikc_queue_desc  recv, send;
	aal_ikc_ph_t               handler;
};

#endif
