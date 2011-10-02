#ifndef HEADER_AAL_IKC_MSG_H
#define HEADER_AAL_IKC_MSG_H

#include <ikc/aal.h>

#define MASTER_PACKET_INIT_ACK 0x10203010

struct aal_ikc_master_packet {
	uint32_t msg;
	uint32_t seq;
	uint64_t param[3];
};

#define MASTER_IKCQ_SIZE    PAGE_SIZE
#define MASTER_IKCQ_PKTSIZE sizeof(struct aal_ikc_master_packet)

#endif
