#include <aal/debug.h>
#include <aal/mm.h>
#include <aal/cpu.h>
#include <types.h>
#include <errno.h>
#include "knf.h"

#define KNF_BOOT_MAGIC_BOOTED       0x25470290
#define KNF_BOOT_MAGIC_READY        0x25470293

extern void main(void);
extern void setup_x86(void);
extern void init_sfi(void);

static unsigned char stack[8192] __attribute__((aligned(4096)));
extern struct aal_kmsg_buf kmsg_buf;

static void *sbox_base = (void *)SBOX_BASE;

void sbox_write(int offset, unsigned int value)
{
	*(volatile unsigned int *)(sbox_base + offset) = value;
}
unsigned int sbox_read(int offset)
{
	return *(volatile unsigned int *)(sbox_base + offset);
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

static void init_smpt(void)
{
	/* 0 - 32 GB */
	sbox_write(SBOX_SMPT00, BUILD_SMPT(SNOOP_ON, 0));
	sbox_write(SBOX_SMPT01, BUILD_SMPT(SNOOP_ON, 1));
	sbox_write(SBOX_SMPT02, BUILD_SMPT(SNOOP_OFF, 0));
	sbox_write(SBOX_SMPT03, BUILD_SMPT(SNOOP_OFF, 1));
}

unsigned long host_to_pa(unsigned long host, int smptentry)
{
	return MIC_SYSTEM_BASE + (host & MIC_SYSTEM_PAGE_MASK)
		+ ((unsigned long)smptentry << MIC_SYSTEM_PAGE_SHIFT);
}

void arch_start(void)
{
	/* Set up initial (temporary) stack */
	asm volatile("movq %0, %%rsp" : : "r" (stack + sizeof(stack)));

	main();

	while (1);
}

void arch_ready(void)
{
	sbox_write(SBOX_SCRATCH12, KNF_BOOT_MAGIC_READY);
}

void arch_init(void)
{
	init_sfi();
	init_smpt();

	/* Notify the address of the kmsg */
	sbox_write(SBOX_SCRATCH14, (unsigned int)(unsigned long)&kmsg_buf);
	/* Ack boot (trampoline code shall be free'd) */
	sbox_write(SBOX_SCRATCH12, KNF_BOOT_MAGIC_BOOTED);

	setup_x86();

	sbox_base = map_fixed_area(SBOX_BASE, SBOX_SIZE, 1);
}

void arch_set_mikc_queue(void *rq, void *wq)
{
	sbox_write(SBOX_SCRATCH13, virt_to_phys(wq));
	sbox_write(SBOX_SCRATCH15, virt_to_phys(rq));
}

void aal_mc_interrupt_host(int vector)
{
	unsigned int reg;
	/* Vector is virtual vector, and we use interrupt 0 anyway */
	reg = sbox_read(SBOX_SDBIC0);
	reg |= 1U << 31;
	sbox_write(SBOX_SDBIC0, reg);
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

int aal_mc_get_vector(enum aal_mc_gv_type type)
{
	switch (type) {
	case AAL_GV_IKC:
		return 0xd1;
	default:
		return -ENOENT;
	}
}

unsigned long aal_mc_map_memory(void *os, unsigned long phys,
                                unsigned long size)
{
	/* TODO: os support (currently, os is ignored and assumed to be Host) */
	return host_to_pa(phys & MIC_SYSTEM_PAGE_MASK,
	                  phys >> MIC_SYSTEM_PAGE_SHIFT);
}

void aal_mc_unmap_memory(void *os, unsigned long phys, unsigned long size)
{
	return;
}
