/**
 * \file ikc/include/ikc/queue.h
 * \brief IHK-IKC: Queue structure definition
 *
 * Copyright (C) 2011-2012 Taku Shimosawa <shimosawa@is.s.u-tokyo.ac.jp>
 */
#ifndef HEADER_IHK_IKC_QUEUE_H
#define HEADER_IHK_IKC_QUEUE_H

#include <ikc/ihk.h>

#define IKC_OS_HOST      ((ihk_os_t)NULL)

struct ihk_ikc_channel_desc;
struct ihk_ikc_queue_desc;

typedef int (*ihk_ikc_ph_t)(struct ihk_ikc_channel_desc *,
                            void *, void *);

struct ihk_ikc_queue_head {
/* 0 */
	uint32_t        id;
	uint16_t        type;
	uint16_t        pktsize;
	uint32_t        pktcount;
	uint32_t        flag;
/* 16 */
	uint64_t        read_off;
	uint64_t        max_read_off;
/* 32 */
	uint64_t        write_off;
	uint64_t        queue_size;
/* 48 */
	uint32_t        channel_id;
	uint32_t        read_cpu;
	uint32_t        write_cpu;
	uint32_t        dummy2;
/* 64 */
};

struct ihk_ikc_queue_desc {
	struct ihk_ikc_queue_head *queue;  /* Virtual address */
	struct ihk_ikc_queue_head  cache;  /* Cache for local reference */
	unsigned long              qrphys; /* Remote physical memory */
	unsigned long              qphys;  /* Local physical memory */
	ihk_spinlock_t             lock;
	uint32_t                   intr_cpu;
};

enum ihk_ikc_channel_flag {
	IKC_FLAG_ENABLED        = 1,
	IKC_FLAG_DESTROYING     = 2,
	IKC_FLAG_DESTROY_ACKED  = 4,
	IKC_FLAG_STATUS_MASK    = 7,
	IKC_FLAG_NO_COPY        = 0x10,
};

struct ihk_ikc_free_packet {
	struct list_head list;
};

struct ihk_ikc_channel_desc {
	struct list_head           list_all;
	ihk_os_t                   remote_os;
	int                        remote_channel_id;
	uint64_t                   remote_channel_va;
	struct ihk_ikc_channel_desc *master;
	int                        port;
	int                        channel_id;
	struct ihk_ikc_queue_desc  recv, send;
	ihk_spinlock_t             lock;
	enum ihk_ikc_channel_flag  flag;
	ihk_ikc_ph_t               handler;
	struct list_head           packet_pool;
	ihk_spinlock_t             packet_pool_lock;
};

struct ihk_ikc_free_packet *ihk_ikc_alloc_packet(struct ihk_ikc_channel_desc *c);
void ihk_ikc_release_packet(struct ihk_ikc_free_packet *p, struct ihk_ikc_channel_desc *c);

int ihk_ikc_init_queue(struct ihk_ikc_queue_head *q,
                       int id, int type, int size, int packetsize);
int ihk_ikc_queue_is_empty(struct ihk_ikc_queue_head *q);
int ihk_ikc_queue_is_full(struct ihk_ikc_queue_head *q);
int ihk_ikc_read_queue(struct ihk_ikc_queue_head *q, void *packet, int flag);
int ihk_ikc_write_queue(struct ihk_ikc_queue_head *q, void *packet, int flag);

struct ihk_ikc_channel_desc *ihk_ikc_create_channel(ihk_os_t os,
                                                    int port,
                                                    int packet_size,
                                                    unsigned long qsize,
                                                    unsigned long *rq,
                                                    unsigned long *sq,
                                                    enum ihk_ikc_channel_flag);
void ihk_ikc_free_channel(struct ihk_ikc_channel_desc *desc);

void ihk_ikc_enable_channel(struct ihk_ikc_channel_desc *channel);
void ihk_ikc_disable_channel(struct ihk_ikc_channel_desc *channel);

void ihk_ikc_channel_set_cpu(struct ihk_ikc_channel_desc *c, int cpu);

#define IKC_NO_NOTIFY    0x100

int ihk_ikc_send(struct ihk_ikc_channel_desc *channel, void *p, int opt);
int ihk_ikc_recv(struct ihk_ikc_channel_desc *channel, void *p, int opt);
int ihk_ikc_recv_handler(struct ihk_ikc_channel_desc *channel, 
                         ihk_ikc_ph_t h, void *harg, int opt);
int ihk_ikc_set_remote_queue(struct ihk_ikc_queue_desc *q, ihk_os_t os,
                             unsigned long rphys, unsigned long qsize);
void ihk_ikc_system_init(ihk_os_t);
void ihk_ikc_system_exit(ihk_os_t);

void ihk_ikc_init_desc(struct ihk_ikc_channel_desc *c,
                       ihk_os_t ros, int cid,
                       struct ihk_ikc_queue_head *rq,
                       struct ihk_ikc_queue_head *wq,
                       ihk_ikc_ph_t packet_handler,
                       struct ihk_ikc_channel_desc *master);
struct ihk_ikc_channel_desc *ihk_ikc_find_channel(ihk_os_t os, int id);

static inline int ihk_ikc_channel_enabled(struct ihk_ikc_channel_desc *c)
{
	return (c->flag & IKC_FLAG_STATUS_MASK) == IKC_FLAG_ENABLED;
}

#endif
