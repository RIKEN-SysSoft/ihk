/**
 * \file ikc/include/ikc/msg.h
 * \brief IHK-IKC: Packet structure definition
 *
 * Copyright (C) 2011-2012 Taku Shimosawa <shimosawa@is.s.u-tokyo.ac.jp>
 */
#ifndef HEADER_IHK_IKC_MSG_H
#define HEADER_IHK_IKC_MSG_H

#include <ikc/ihk.h>

#define IHK_IKC_MASTER_MSG_INIT_ACK      0x10203010
#define IHK_IKC_MASTER_MSG_CONNECT       0x20000001
#define IHK_IKC_MASTER_MSG_CONNECT_REPLY 0x20000002
#define IHK_IKC_MASTER_MSG_DISCONNECT    0x20000008
#define IHK_IKC_MASTER_MSG_PACKET_ON_CHANNEL 0x20000010

struct ihk_ikc_master_packet {
	uint32_t msg;
	uint32_t ref;
	uint64_t param[5];
};

#define MASTER_IKCQ_SIZE    PAGE_SIZE
#define MASTER_IKCQ_PKTSIZE sizeof(struct ihk_ikc_master_packet)

int ihk_ikc_master_channel_packet_handler(struct ihk_ikc_channel_desc *c,
                                          void *__packet, void *os);

struct ikc_test_packet { 
	uint32_t msg;
	uint32_t param1;
};

#endif
