#include <aal/debug.h>
#include <aal/mm.h>
#include <errno.h>

/* MEE Setup.c */
struct shimos_boot_param {
	unsigned long start, end;
	unsigned long cores;
	unsigned long status;
	unsigned long msg_buffer;
};

static unsigned char stack[8192] __attribute__((aligned(4096)));

unsigned long boot_param_pa;
struct shimos_boot_param *boot_param;

extern void main(void);
extern void setup_x86(void);
extern struct aal_kmsg_buf kmsg_buf;

void arch_start(unsigned long param_addr)
{
	boot_param = (struct shimos_boot_param *)param_addr;
	boot_param_pa = param_addr;

	/* Set up initial (temporary) stack */
	asm volatile("movq %0, %%rsp" : : "r" (stack + sizeof(stack)));

	main();

	while (1);
}

void arch_init(void)
{
	/* Ack boot (trampoline code shall be free'd) */
	boot_param->msg_buffer = (unsigned long)&kmsg_buf;
	boot_param->status = 1;

	setup_x86();
	boot_param = map_fixed_area(boot_param_pa, sizeof(*boot_param), 0);
}

unsigned long aal_mc_get_memory_address(enum aal_mc_gma_type type, int opt)
{
	switch (type) {
	case AAL_MC_GMA_MAP_START:
	case AAL_MC_GMA_AVAIL_START:
		return boot_param->start;
	case AAL_MC_GMA_MAP_END:
	case AAL_MC_GMA_AVAIL_END:
		return boot_param->end;
	case AAL_MC_GMA_HEAP_START:
		return virt_to_phys(get_last_early_heap());
	}

	return -ENOENT;
}

void __reserve_arch_pages(unsigned long start, unsigned long end,
                          void (*cb)(unsigned long, unsigned long, int))
{
	/* No hole */
}

