/**
 * \file ikc/include/ikc/queue.h
 * \brief AAL-IKC: Queue structure definition
 *
 * Copyright (C) 2011-2012 Taku Shimosawa <shimosawa@is.s.u-tokyo.ac.jp>
 */
#ifndef HEADER_AAL_IKC_QUEUE_H
#define HEADER_AAL_IKC_QUEUE_H

#include <ikc/ihk.h>

#define IKC_OS_HOST      ((aal_os_t)NULL)

struct aal_ikc_channel_desc;
struct aal_ikc_queue_desc;

typedef int (*aal_ikc_ph_t)(struct aal_ikc_channel_desc *,
                            void *, void *);

struct aal_ikc_queue_head {
/* 0 */
	uint32_t        id;
	uint16_t        type;
	uint16_t        pktsize;
	uint32_t        pktcount;
	uint32_t        flag;
/* 16 */
	uint64_t        read_off;
	uint64_t        write_off;
/* 32 : Receiver */
	uint32_t        channel_id;
	uint32_t        dummy;
	uint64_t        queue_size;
/* 48 */
	uint32_t        read_cpu;
	uint32_t        write_cpu;
	uint64_t        dummy2;
/* 64 */
};

struct aal_ikc_queue_desc {
	struct aal_ikc_queue_head *queue;  /* Virtual address */
	struct aal_ikc_queue_head  cache;  /* Cache for local reference */
	unsigned long              qrphys; /* Remote physical memory */
	unsigned long              qphys;  /* Local physical memory */
	aal_spinlock_t             lock;
	uint32_t                   intr_cpu;
};

enum aal_ikc_channel_flag {
	IKC_FLAG_ENABLED        = 1,
	IKC_FLAG_DESTROYING     = 2,
	IKC_FLAG_DESTROY_ACKED  = 4,
	IKC_FLAG_STATUS_MASK    = 7,
	IKC_FLAG_NO_COPY        = 0x10,
};

struct aal_ikc_channel_desc {
	struct list_head           all_list;
	struct list_head           list;
	aal_os_t                   remote_os;
	int                        remote_channel_id;
	int                        port;
	int                        channel_id;
	struct aal_ikc_queue_desc  recv, send;
	aal_spinlock_t             lock;
	enum aal_ikc_channel_flag  flag;
	aal_ikc_ph_t               handler;
	char                       packet_buf[0];
};

int aal_ikc_init_queue(struct aal_ikc_queue_head *q,
                       int id, int type, int size, int packetsize);
int aal_ikc_queue_is_empty(struct aal_ikc_queue_head *q);
int aal_ikc_queue_is_full(struct aal_ikc_queue_head *q);
int aal_ikc_read_queue(struct aal_ikc_queue_head *q, void *packet, int flag);
int aal_ikc_write_queue(struct aal_ikc_queue_head *q, void *packet, int flag);

struct aal_ikc_channel_desc *aal_ikc_create_channel(aal_os_t os,
                                                    int port,
                                                    int packet_size,
                                                    unsigned long qsize,
                                                    unsigned long *rq,
                                                    unsigned long *sq,
                                                    enum aal_ikc_channel_flag);
void aal_ikc_free_channel(struct aal_ikc_channel_desc *desc);

void aal_ikc_enable_channel(struct aal_ikc_channel_desc *channel);
void aal_ikc_disable_channel(struct aal_ikc_channel_desc *channel);

void aal_ikc_channel_set_cpu(struct aal_ikc_channel_desc *c, int cpu);

#define IKC_NO_NOTIFY    0x100

int aal_ikc_send(struct aal_ikc_channel_desc *channel, void *p, int opt);
int aal_ikc_recv(struct aal_ikc_channel_desc *channel, void *p, int opt);
int aal_ikc_recv_handler(struct aal_ikc_channel_desc *channel, 
                         aal_ikc_ph_t h, void *harg, int opt);
int aal_ikc_set_remote_queue(struct aal_ikc_queue_desc *q, aal_os_t os,
                             unsigned long rphys, unsigned long qsize);
void aal_ikc_system_init(aal_os_t);
void aal_ikc_system_exit(aal_os_t);

void aal_ikc_init_desc(struct aal_ikc_channel_desc *c,
                       aal_os_t ros, int cid,
                       struct aal_ikc_queue_head *rq,
                       struct aal_ikc_queue_head *wq,
                       aal_ikc_ph_t packet_handler);
struct aal_ikc_channel_desc *aal_ikc_find_channel(aal_os_t os, int id);

static inline int aal_ikc_channel_enabled(struct aal_ikc_channel_desc *c)
{
	return (c->flag & IKC_FLAG_STATUS_MASK) == IKC_FLAG_ENABLED;
}

#endif
