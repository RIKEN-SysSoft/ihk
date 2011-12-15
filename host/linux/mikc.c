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
#include <aal/misc/debug.h>
#include <ikc/queue.h>
#include <ikc/msg.h>
#include "host_linux.h"

static int arch_master_handler(struct aal_ikc_channel_desc *c,
                               void *__packet, void *__os);

struct aal_ikc_channel_desc *aal_host_ikc_init_first(aal_os_t aal_os,
                                                     aal_ikc_ph_t handler)
{
	struct aal_host_linux_os_data *os = aal_os;
	unsigned long r, w, rp, wp, rsz, wsz;
	struct aal_ikc_queue_head *rq, *wq;
	struct aal_ikc_channel_desc *c;

	aal_ikc_system_init(aal_os);
	os->ikc_initialized = 1;

	if (aal_os_wait_for_status(aal_os, AAL_OS_STATUS_READY, 0, 30) == 0) {
		/* XXX: 
		 * We assume this address is remote, 
		 * but the local is possible... */
		dprintf("OS is now marked ready.\n");

		aal_os_get_special_address(aal_os, AAL_SPADDR_MIKC_QUEUE_RECV,
		                           &r, &rsz);
		aal_os_get_special_address(aal_os, AAL_SPADDR_MIKC_QUEUE_SEND,
		                           &w, &wsz);

		rp = aal_device_map_memory(os->dev_data, r, rsz);
		wp = aal_device_map_memory(os->dev_data, w, wsz);
		
		rq = aal_device_map_virtual(os->dev_data, rp, rsz, NULL, 0);
		wq = aal_device_map_virtual(os->dev_data, wp, wsz, NULL, 0);

		c = kzalloc(sizeof(struct aal_ikc_channel_desc)
		            + sizeof(struct aal_ikc_master_packet), GFP_KERNEL);
		aal_ikc_init_desc(c, aal_os, 0, rq, wq,
		                  aal_ikc_master_channel_packet_handler);

		c->recv.qphys = rp;
		c->send.qphys = wp;
		c->recv.qrphys = r;
		c->send.qrphys = w;

		printk("c->remote_os = %p\n", c->remote_os);
		os->packet_handler = handler;

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

	os->mchannel = 
		aal_host_ikc_init_first(os, arch_master_handler);
	printk("os(%p)->mchannel = %p\n", os, os->mchannel);
	if (!os->mchannel) {
		return -EINVAL;
	} else {
		aal_ikc_enable_channel(os->mchannel);

		printk("ikc_master_init done.\n");

		/* ack send */
		packet.msg = AAL_IKC_MASTER_MSG_INIT_ACK;
		aal_ikc_send(os->mchannel, &packet, 0);

		return 0;
	}
}

void aal_ikc_destroy_channel(aal_os_t __os, struct aal_ikc_channel_desc *c)
{
	if (!c) {
		return;
	}
	aal_ikc_disable_channel(c);
	aal_ikc_free_channel(c);
}

void ikc_master_finalize(aal_os_t __os)
{
	struct aal_host_linux_os_data *os = __os;

	dprint_func_enter;

	if (!os->ikc_initialized) {
		return;
	}

	if (os->mchannel) {
		aal_ikc_destroy_channel(os, os->mchannel);
	}
	aal_ikc_system_exit(os);

	os->ikc_initialized = 0;
}

struct list_head *aal_os_get_ikc_channel_list(aal_os_t aal_os)
{
	struct aal_host_linux_os_data *os = aal_os;

	return &os->ikc_channels;
}

spinlock_t *aal_os_get_ikc_channel_lock(aal_os_t aal_os)
{
	struct aal_host_linux_os_data *os = aal_os;

	return &os->ikc_channel_lock;
}

struct aal_host_interrupt_handler *aal_host_os_get_ikc_handler(aal_os_t aal_os)
{
	struct aal_host_linux_os_data *os = aal_os;

	return &os->ikc_handler;
}

int aal_ikc_send_interrupt(struct aal_ikc_channel_desc *channel)
{
	return aal_os_issue_interrupt(channel->remote_os, 
	                              channel->send.queue->read_cpu,
	                              0xd1);
}

aal_spinlock_t *aal_ikc_get_listener_lock(aal_os_t aal_os)
{
	struct aal_host_linux_os_data *os = aal_os;

	return &os->listener_lock;
}

struct aal_ikc_listen_param **aal_ikc_get_listener_entry(aal_os_t aal_os,
                                                         int port)
{
	struct aal_host_linux_os_data *os = aal_os;

	return &(os->listeners[port]);
}

int aal_ikc_call_master_packet_handler(aal_os_t aal_os,
                                       struct aal_ikc_channel_desc *c,
                                       void *packet)
{
	struct aal_host_linux_os_data *os = aal_os;

	if (os->packet_handler) {
		return os->packet_handler(c, packet, os);
	}
	return 0;
}

static int arch_master_handler(struct aal_ikc_channel_desc *c,
                               void *__packet, void *__os)
{
	struct aal_ikc_master_packet *packet = __packet;

	/* TODO */
	printk("Unhandled master packet! : %x\n", packet->msg);
	return 0;
}

struct list_head *aal_ikc_get_master_wait_list(aal_os_t aal_os)
{
	struct aal_host_linux_os_data *os = aal_os;

	return &os->wait_list;
}
aal_spinlock_t *aal_ikc_get_master_wait_lock(aal_os_t aal_os)
{
	struct aal_host_linux_os_data *os = aal_os;

	return &os->wait_lock;
}

struct aal_ikc_channel_desc *aal_os_get_master_channel(aal_os_t __os)
{
	struct aal_host_linux_os_data *os = __os;

	printk("os(%p)->mchannel = %p\n", os, os->mchannel);
	return os->mchannel;
}

int aal_os_get_unique_channel_id(aal_os_t aal_os)
{
	struct aal_host_linux_os_data *os = aal_os;
	
	return atomic_inc_return(&os->channel_id);
}

void aal_ikc_linux_init_work_data(aal_os_t aal_os,
                                  void (*f)(struct work_struct *))
{
	struct aal_host_linux_os_data *os = aal_os;

	INIT_WORK(&os->ikc_work, f);
}

void aal_ikc_linux_schedule_work(aal_os_t aal_os)
{
	struct aal_host_linux_os_data *os = aal_os;

	schedule_work(&os->ikc_work);
}

aal_os_t aal_ikc_linux_get_os_from_work(struct work_struct *work)
{
	return container_of(work, struct aal_host_linux_os_data, ikc_work);
}
