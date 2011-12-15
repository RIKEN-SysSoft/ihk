#include <aal/cpu.h>
#include <aal/ikc.h>
#include <aal/lock.h>
#include <memory.h>
#include <string.h>
#include "bootparam.h"

extern int aal_mc_ikc_init_first_local(struct aal_ikc_channel_desc *channel,
                                       int (*h)(struct aal_ikc_channel_desc *,
                                                void *, void *));

int aal_mc_ikc_init_first(struct aal_ikc_channel_desc *channel,
                          int (*packet_handler)(struct aal_ikc_channel_desc *,
                                                void *, void *))
{
	return aal_mc_ikc_init_first_local(channel, packet_handler);
}

int aal_ikc_send_interrupt(struct aal_ikc_channel_desc *channel)
{	
	return aal_mc_interrupt_host(channel->recv.queue->write_cpu,
	                             AAL_GV_IKC);
}
