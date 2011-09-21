#include <aal/debug.h>

/* MEE Setup.c */
struct shimos_boot_param {
	unsigned long start, end;
	unsigned long cores;
	unsigned long status;
	unsigned long msg_buffer;
};

static unsigned char stack[8192] __attribute__((aligned(4096)));

struct shimos_boot_param *boot_param;

extern void main(void);
extern struct aal_kmsg_buf kmsg_buf;

void arch_start(unsigned long param_addr)
{
	boot_param = (struct shimos_boot_param *)param_addr;

	/* Set up initial (temporary) stack */
	asm volatile("leaq %0, %%rsp" : : "m" (stack[sizeof(stack)]));

	main();

	while (1);
}

void arch_init(void)
{
	/* Ack boot (trampoline code shall be free'd) */
	boot_param->msg_buffer = (unsigned long)&kmsg_buf;
	boot_param->status = 1;
}
