#ifndef HEADER_AAL_IKC_MSG_H
#define HEADER_AAL_IKC_MSG_H

#include <ikc/aal.h>

#define AAL_IKC_MASTER_MSG_INIT_ACK      0x10203010
#define AAL_IKC_MASTER_MSG_CONNECT       0x20000001
#define AAL_IKC_MASTER_MSG_CONNECT_REPLY 0x20000002
#define AAL_IKC_MASTER_MSG_DISCONNECT    0x20000008

struct aal_ikc_master_packet {
	uint32_t msg;
	uint32_t ref;
	uint64_t param[3];
};

#define MASTER_IKCQ_SIZE    PAGE_SIZE
#define MASTER_IKCQ_PKTSIZE sizeof(struct aal_ikc_master_packet)

int aal_ikc_master_channel_packet_handler(struct aal_ikc_channel_desc *c,
                                          void *__packet, void *os);

struct ikc_test_packet { 
	uint32_t msg;
	uint32_t param1;
};

#endif
