#include <aal/debug.h>
#include <aal/mm.h>
#include <errno.h>
#include "knf.h"

#define KNF_BOOT_MAGIC_BOOTED       0x25470290
#define KNF_BOOT_MAGIC_READY        0x25470293

extern void main(void);
extern void setup_x86(void);
extern void init_sfi(void);

static unsigned char stack[8192] __attribute__((aligned(4096)));
extern struct aal_kmsg_buf kmsg_buf;

void sbox_write(int offset, unsigned int value)
{
	*(volatile unsigned int *)(SBOX_BASE + offset) = value;
}
unsigned int sbox_read(int offset)
{
	return *(volatile unsigned int *)(SBOX_BASE + offset);
}
void gtt_write(int index, unsigned long phys, unsigned int enable)
{
	*(volatile unsigned int *)(MIC_GTT_BASE + (unsigned long)index * 4)
		= (unsigned int)(phys >> 11) | enable;
}
unsigned int gtt_read(int index)
{
	return *(volatile unsigned int *)(MIC_GTT_BASE + (unsigned long)index * 4);
}

void arch_start(void)
{
	/* Set up initial (temporary) stack */
	asm volatile("movq %0, %%rsp" : : "r" (stack + sizeof(stack)));

	main();

	while (1);
}

unsigned long get_cr3(void)
{
	unsigned long a;

	asm volatile("movq %%cr3, %0" : "=r"(a));

	return a;
}

void set_cr3(unsigned long phys)
{
	asm volatile("movq %0, %%cr3" : : "r"(phys));
}

void flush_tlb(void)
{
	set_cr3(get_cr3());
}

void arch_init(void)
{
	init_sfi();

	/* Notify the address of the kmsg */
	sbox_write(SBOX_SCRATCH14, (unsigned int)(unsigned long)&kmsg_buf);
	/* Ack boot (trampoline code shall be free'd) */
	sbox_write(SBOX_SCRATCH12, KNF_BOOT_MAGIC_BOOTED);

	setup_x86();
}


extern unsigned long sfi_get_memory_address(enum aal_mc_gma_type type, int opt);

unsigned long aal_mc_get_memory_address(enum aal_mc_gma_type type, int opt)
{
	switch (type) {
	case AAL_MC_GMA_MAP_START:
	case AAL_MC_GMA_MAP_END:
	case AAL_MC_GMA_AVAIL_START:
	case AAL_MC_GMA_AVAIL_END:
		return sfi_get_memory_address(type, opt);
	case AAL_MC_GMA_HEAP_START:
		return virt_to_phys(get_last_early_heap());
	}

	return -ENOENT;
}
