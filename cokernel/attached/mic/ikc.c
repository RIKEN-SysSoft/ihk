#include <ihk/cpu.h>
#include <ihk/ikc.h>
#include <ihk/lock.h>
#include <memory.h>
#include <string.h>

extern int ihk_mc_ikc_init_first_local(struct ihk_ikc_channel_desc *channel,
                                       int (*h)(struct ihk_ikc_channel_desc *,
                                                void *, void *));

int ihk_mc_ikc_init_first(struct ihk_ikc_channel_desc *channel,
                          int (*packet_handler)(struct ihk_ikc_channel_desc *,
                                                void *, void *))
{
	return ihk_mc_ikc_init_first_local(channel, packet_handler);
}

int ihk_ikc_send_interrupt(struct ihk_ikc_channel_desc *channel)
{
	return ihk_mc_interrupt_host(channel->recv.queue->write_cpu, 0);
}
