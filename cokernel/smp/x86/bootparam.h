#ifndef HEADER_BUILTIN_BOOTPARAM_H
#define HEADER_BUILTIN_BOOTPARAM_H

#define SMP_MAX_CPUS 512

#define __NCOREBITS  (sizeof(long) * 8)   /* bits per mask */
#define CORE_SET(n, p) \
	((p).set[(n)/__NCOREBITS] |= ((long)1 << ((n) % __NCOREBITS)))
#define CORE_CLR(n, p) \
	((p).set[(n)/__NCOREBITS] &= ~((long)1 << ((n) % __NCOREBITS)))
#define CORE_ISSET(n, p) \
	(((p).set[(n)/__NCOREBITS] & ((long)1 << ((n) % __NCOREBITS)))?1:0)
#define CORE_ZERO(p)      memset(&(p).set, 0, sizeof((p).set))

struct smp_coreset {
	unsigned long set[SMP_MAX_CPUS / __NCOREBITS];
};

static inline int CORE_ISSET_ANY(struct smp_coreset *p)
{
	int     i;

	for(i = 0; i < SMP_MAX_CPUS / __NCOREBITS; i++)
		if(p -> set[i])
			return 1;
	return 0;
}

struct smp_boot_param {
	unsigned long start, end;
	unsigned long status;
	struct smp_coreset coreset;
	unsigned long msg_buffer;
	unsigned long mikc_queue_recv, mikc_queue_send;

	unsigned long dma_address;
	unsigned long ident_table;
	unsigned long ns_per_tsc;
	unsigned long boot_sec;
	unsigned long boot_nsec;
	unsigned int ihk_ikc_irq;
	unsigned int ihk_ikc_irq_apicid;
	char kernel_args[256];
};

#endif
