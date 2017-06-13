/**
 * \file ikc/include/ikc/master.h
 * \brief IHK-IKC: Structures and functions in master channel
 *
 * Copyright (C) 2011-2012 Taku Shimosawa <shimosawa@is.s.u-tokyo.ac.jp>
 */
#ifndef HEADER_IHK_IKC_MASTER_H
#define HEADER_IHK_IKC_MASTER_H

#include <ikc/queue.h>
#include <ikc/msg.h>

#define IHK_IKC_MAX_PORT   512

struct ihk_ikc_channel_info;

enum ihk_ikc_direction {
	IHK_IKC_DIRECTION_SEND,
	IHK_IKC_DIRECTION_RECV,
};

struct ihk_ikc_listen_param {
	int (*handler)(struct ihk_ikc_channel_info *);

	int port;
	enum ihk_ikc_direction ikc_direction;
	int pkt_size;
	int queue_size;
	int magic;
};

struct ihk_ikc_connect_param {
	int port;
	int pkt_size;
	int queue_size;
	int magic;
	int intr_cpu;
	ihk_ikc_ph_t               handler;

	struct ihk_ikc_channel_desc *channel;
};

struct ihk_ikc_channel_info {
/* filled by master packet handler */
	struct ihk_ikc_channel_desc *channel;
/* filled by listen handler */
	ihk_ikc_ph_t packet_handler;
};

struct ihk_ikc_master_wait_struct {
	struct list_head list;
	ihk_wait_t       wait;
	int status;
	uint32_t msg;
	uint32_t ref;
	struct ihk_ikc_master_packet res;
};

int ihk_ikc_listen_port(ihk_os_t os, struct ihk_ikc_listen_param *param);
int ihk_ikc_connect(ihk_os_t os, struct ihk_ikc_connect_param *p);
int ihk_ikc_disconnect(struct ihk_ikc_channel_desc *c);
void ihk_ikc_destroy_channel(struct ihk_ikc_channel_desc *c);

#endif
