#include <aal/cpu.h>
#include <aal/ikc.h>
#include <aal/lock.h>
#include <memory.h>
#include <string.h>

extern int aal_mc_ikc_init_first_local(struct aal_ikc_channel_desc *channel,
                                       int (*packet_handler)(void *, void *));

int aal_mc_ikc_init_first(struct aal_ikc_channel_desc *channel,
                          int (*packet_handler)(void *, void *))
{
	return aal_mc_ikc_init_first_local(channel, packet_handler);
}

int aal_ikc_send_interrupt(struct aal_ikc_channel_desc *channel)
{
	return aal_mc_interrupt_host(0);
}
