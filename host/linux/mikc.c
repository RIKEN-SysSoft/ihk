#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/file.h>
#include <asm/spinlock.h>
#include <aal/aal_host_user.h>
#include <aal/aal_host_driver.h>
#include <ikc/queue.h>
#include <ikc/msg.h>
#include "host_linux.h"

static int master_channel_packet_handler(void *__packet, void *__os);

struct aal_ikc_channel_desc *aal_host_ikc_init_first(aal_os_t aal_os,
                                                     int (*handler)
                                                     (void *, void *))
{
	struct aal_host_linux_os_data *os = aal_os;
	unsigned long r, w, rp, wp, rsz, wsz;
	struct aal_ikc_queue_head *rq, *wq;
	struct aal_ikc_channel_desc *c;

	aal_ikc_system_init(aal_os);

	if (aal_os_wait_for_status(aal_os, AAL_OS_STATUS_READY, 0, 30) == 0) {
		/* XXX: 
		 * We assume this address is remote, 
		 * but the local is possible... */
		aal_os_get_special_address(aal_os, AAL_SPADDR_MIKC_QUEUE_RECV,
		                           &r, &rsz);
		aal_os_get_special_address(aal_os, AAL_SPADDR_MIKC_QUEUE_SEND,
		                           &w, &wsz);

		rp = aal_os_map_memory(aal_os, r, rsz);
		wp = aal_os_map_memory(aal_os, w, wsz);
		
		rq = aal_device_map_virtual(os->dev_data, rp, rsz, NULL, 0);
		wq = aal_device_map_virtual(os->dev_data, wp, wsz, NULL, 0);

		c = kzalloc(sizeof(struct aal_ikc_channel_desc), GFP_KERNEL);

		INIT_LIST_HEAD(&c->list);
		c->handler = handler;
		c->remote_os = aal_os;
		c->channel_id = 0;

		spin_lock_init(&c->recv.lock);
		spin_lock_init(&c->send.lock);
		c->recv.queue = rq;
		c->send.queue = wq;

		printk("c->remote_os = %p\n", c->remote_os);

		return c;
	} else {
		printk("AAL: OS does not become ready.\n");
		return NULL;
	}
}

int ikc_master_init(aal_os_t __os)
{
	struct aal_host_linux_os_data *os = __os;
	struct aal_ikc_master_packet packet;

	printk("ikc_master_init\n");
	INIT_LIST_HEAD(&os->ikc_channels);

	os->mchannel = 
		aal_host_ikc_init_first(os, master_channel_packet_handler);
	if (!os->mchannel) {
		return -EINVAL;
	} else {
		aal_ikc_enable_channel(os->mchannel);

		printk("ikc_master_init done.\n");

		/* ack send */
		packet.msg = MASTER_PACKET_INIT_ACK;
		aal_ikc_send(os->mchannel, &packet, 0);

		return 0;
	}
}

static int master_channel_packet_handler(void *__packet, void *__os)
{
	struct aal_ikc_master_packet *packet = __packet;
	struct aal_host_linux_os_data *os = __os;

	/* TODO */
	printk("Master packet! : %x\n", packet->msg);
	return 0;
}

struct list_head *aal_host_os_get_ikc_channel_list(aal_os_t aal_os)
{
	struct aal_host_linux_os_data *os = aal_os;

	return &os->ikc_channels;
}

struct aal_host_interrupt_handler *aal_host_os_get_ikc_handler(aal_os_t aal_os)
{
	struct aal_host_linux_os_data *os = aal_os;

	return &os->ikc_handler;
}

int aal_ikc_send_interrupt(struct aal_ikc_channel_desc *channel)
{
	return aal_os_issue_interrupt(channel->remote_os, 0, 0xd1);
}
