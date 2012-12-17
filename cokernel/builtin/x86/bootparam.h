#ifndef HEADER_BUILTIN_BOOTPARAM_H
#define HEADER_BUILTIN_BOOTPARAM_H

struct shimos_boot_param {
	unsigned long start, end;
	unsigned long cores;
	unsigned long status;
	unsigned long msg_buffer;
	unsigned long mikc_queue_recv, mikc_queue_send;

	unsigned long dma_address;
	unsigned long ident_table;
	char kernel_args[256];
};

extern struct shimos_boot_param *boot_param;

#endif
