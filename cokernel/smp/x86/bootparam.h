#ifndef HEADER_BUILTIN_BOOTPARAM_H
#define HEADER_BUILTIN_BOOTPARAM_H

#define SHIMOS_MAX_CORES 256

#define __NCOREBITS  (sizeof(long) * 8)   /* bits per mask */
#define CORE_SET(n, p) \
	((p).set[(n)/__NCOREBITS] |= ((long)1 << ((n) % __NCOREBITS)))
#define CORE_CLR(n, p) \
	((p).set[(n)/__NCOREBITS] &= ~((long)1 << ((n) % __NCOREBITS)))
#define CORE_ISSET(n, p) \
	(((p).set[(n)/__NCOREBITS] & ((long)1 << ((n) % __NCOREBITS)))?1:0)
#define CORE_ZERO(p)      memset(&(p).set, 0, sizeof((p).set))

typedef struct {
	unsigned long set[SHIMOS_MAX_CORES / __NCOREBITS];
} shimos_coreset;

static inline int CORE_ISSET_ANY(shimos_coreset *p)
{
	int     i;

	for(i = 0; i < SHIMOS_MAX_CORES / __NCOREBITS; i++)
		if(p -> set[i])
			return 1;
	return 0;
}

struct shimos_boot_param {
	unsigned long start, end;
	unsigned long status;
	shimos_coreset coreset;
	unsigned long msg_buffer;
	unsigned long mikc_queue_recv, mikc_queue_send;

	unsigned long dma_address;
	unsigned long ident_table;
	char kernel_args[256];
};

extern struct shimos_boot_param *boot_param;

#endif
