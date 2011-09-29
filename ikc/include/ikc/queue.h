#ifndef HEADER_AAL_IKC_QUEUE_H
#define HEADER_AAL_IKC_QUEUE_H

#include <types.h>
#include <aal/ikc.h>

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
	uint64_t        dummy;
	uint64_t        queue_size;
/* 48 */
	uint32_t        read_cpu;
	uint32_t        write_cpu;
/* 64 */
};

int aal_ikc_init_queue(struct aal_ikc_queue_head *q,
                       int id, int type, int size, int packetsize);
int aal_ikc_queue_is_empty(struct aal_ikc_queue_head *q);
int aal_ikc_queue_is_full(struct aal_ikc_queue_head *q);
int aal_ikc_read_queue(struct aal_ikc_queue_head *q, void *packet, int flag);
int aal_ikc_write_queue(struct aal_ikc_queue_head *q, void *packet, int flag);

void aal_ikc_enable_channel(struct aal_ikc_channel_desc *channel);
void aal_ikc_disable_channel(struct aal_ikc_channel_desc *channel);
int aal_ikc_send(struct aal_ikc_channel_desc *channel, void *p, int opt);
int aal_ikc_recv(struct aal_ikc_channel_desc *channel, void *p, int opt);

void aal_ikc_system_init(void);
#endif
