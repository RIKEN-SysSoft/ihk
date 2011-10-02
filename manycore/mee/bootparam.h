#ifndef HEADER_MEE_BOOTPARAM_H
#define HEADER_MEE_BOOTPARAM_H

struct shimos_boot_param {
	unsigned long start, end;
	unsigned long cores;
	unsigned long status;
	unsigned long msg_buffer;
	unsigned long mikc_queue_recv, mikc_queue_send;
};

extern struct shimos_boot_param *boot_param;

#endif
