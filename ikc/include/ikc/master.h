#ifndef HEADER_AAL_IKC_MASTER_H
#define HEADER_AAL_IKC_MASTER_H

#include <ikc/queue.h>
#include <ikc/msg.h>

#define AAL_IKC_MAX_PORT   512

struct aal_ikc_channel_info;

struct aal_ikc_listen_param {
	struct list_head list;
	int (*handler)(struct aal_ikc_channel_info *);

	int port;
	int pkt_size;
	int queue_size;
	int magic;
};

struct aal_ikc_connect_param {
	int port;
	int pkt_size;
	int queue_size;
	int magic;
	aal_ikc_ph_t               handler;

	struct aal_ikc_channel_desc *channel;
};

struct aal_ikc_channel_info {
/* filled by master packet handler */
	struct aal_ikc_channel_desc *channel;
	struct aal_ikc_listen_param *listen_param;

/* filled by listen handler */
	aal_ikc_ph_t packet_handler;
	void *handler_private;
};

struct aal_ikc_master_wait_struct {
	struct list_head list;
	aal_wait_t       wait;
	int status;
	uint32_t msg;
	uint32_t ref;
	struct aal_ikc_master_packet res;
};

int aal_ikc_listen_port(aal_os_t os, struct aal_ikc_listen_param *param);
int aal_ikc_connect(aal_os_t os, struct aal_ikc_connect_param *p);
#endif
