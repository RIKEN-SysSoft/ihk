#ifndef HEADER_X86_COMMON_AAL_IKC_H
#define HEADER_X86_COMMON_AAL_IKC_H

#include <ikc/msg.h>
#include <aal/lock.h>
#include <types.h>

#define IKC_DEST_HOST        0

struct aal_ikc_queue_head;

struct aal_ikc_queue_desc {
	struct aal_ikc_queue_head *queue;  /* Virtual address */
	aal_spinlock_t             lock;
	uint32_t                   intr_cpu;
};

struct aal_ikc_channel_desc {
	struct list_head              list;
	int                           remote_id;
	int                           channel_id;
	struct aal_ikc_queue_desc  recv, send;
	int (*handler)(void *, void *);
};

/* manycore side */
int aal_mc_ikc_init_first(struct aal_ikc_channel_desc *,
                          int handler(void *, void *));
#include <ikc/queue.h>

#endif

