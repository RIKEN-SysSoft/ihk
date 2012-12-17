#include <aal/debug.h>
#include <aal/mm.h>
#include <aal/cpu.h>
#include <aal/perfctr.h>
#include <types.h>
#include <errno.h>
#include <string.h>
#include <registers.h>
#include <march.h>
#include "knf.h"
#include <sysdeps/knf/knf_host.h>

#define KNF_BOOT_MAGIC_BOOTED       0x25470290
#define KNF_BOOT_MAGIC_READY        0x25470293

extern void main(void);
extern void setup_x86(void);
extern void init_sfi(void);

static unsigned char stack[8192] __attribute__((aligned(4096)));
extern struct aal_kmsg_buf kmsg_buf;

static struct knf_boot_param *boot_param;

void gtt_write(int index, unsigned long phys, unsigned int enable)
{
	*(volatile unsigned int *)(MIC_GTT_BASE + (unsigned long)index * 4)
		= (unsigned int)(phys >> 11) | enable;
}
unsigned int gtt_read(int index)
{
	return *(volatile unsigned int *)(MIC_GTT_BASE
	                                  + (unsigned long)index * 4);
}

#define KNC_MAP_TEST
#ifdef KNC_MAP_TEST
#define SMTP_ADDR(phys) (phys >> 34)
unsigned long db_smtp_phys;
#endif

static void init_smpt(void)
{
#ifdef CONFIG_KNF
	/* 0 - 32 GB */
	sbox_write(SBOX_SMPT00, BUILD_SMPT(SNOOP_ON, 0));
	sbox_write(SBOX_SMPT01, BUILD_SMPT(SNOOP_ON, 1));
	sbox_write(SBOX_SMPT02, BUILD_SMPT(SNOOP_OFF, 0));
	sbox_write(SBOX_SMPT03, BUILD_SMPT(SNOOP_OFF, 1));
#else
	int i;
	unsigned int smpt_reg_offset = SBOX_SMPT00;

#ifdef KNC_MAP_MICPA
    /* spare some pages for map MIC phys addr to PCI addr */
	for(i = 0; i < NUM_SMPT_ENTRIES_IN_USE - NUM_SMPT_ENTRIES_MICPA; i++) {
        sbox_write(smpt_reg_offset, BUILD_SMPT(SNOOP_ON, i));
		smpt_reg_offset += 4;
    }
#else
	/* 0 - 512GB */
	for(i = 0; i < NUM_SMPT_ENTRIES_IN_USE; i++) {
#ifdef KNC_MAP_TEST
		if(i == NUM_SMPT_ENTRIES_IN_USE - 1 ) {
			db_smtp_phys = SMTP_ADDR(0xec00000000ULL);
			sbox_write(smpt_reg_offset, BUILD_SMPT(SNOOP_ON, db_smtp_phys)); /* 0xfc0000 0000 -> 0xec 0000 0000 */
		} else
#endif
		{
			sbox_write(smpt_reg_offset, BUILD_SMPT(SNOOP_ON, i));
		}
		smpt_reg_offset += 4;
	}
#endif /* KNC_MAP_MICPA */
#endif /* CONFIG_KNF */

#if 0
	uint64_t host_physaddr = 0;
	uint32_t smpt_reg_offset = SBOX_SMPT00;
	uint32_t smpt_reg_val;
	int i;

	for (i = 0; i < NUM_SMPT_ENTRIES_IN_USE; i++) {

		smpt_reg_val = BUILD_SMPT(SNOOP_ON, host_physaddr >> 
		                          MIC_SYSTEM_PAGE_SHIFT);

		sbox_write(smpt_reg_offset, smpt_reg_val);
		//writel(smpt_reg_val, (uint8_t*)mm_sbox + smpt_reg_offset);
		smpt_reg_offset += 4;
		host_physaddr += MIC_SYSTEM_PAGE_SIZE;
	}
#endif
}

unsigned long host_to_pa(unsigned long host, int smptentry)
{
	return MIC_SYSTEM_BASE + (host & MIC_SYSTEM_PAGE_MASK)
		+ ((unsigned long)smptentry << MIC_SYSTEM_PAGE_SHIFT);
}

static void setup_boot_param(void)
{
	unsigned long low, high;

	high = sbox_read(SBOX_SCRATCH14);
	low = sbox_read(SBOX_SCRATCH15);

	/* Unfortunately, it cannot be accessed because it's not mapped. */
	boot_param = (struct knf_boot_param *)
		aal_mc_map_memory(NULL, ((high << 32L) | low),
		                  sizeof(*boot_param));
}

unsigned long x86_kernel_phys_base;

void arch_start(unsigned long param, unsigned long phys_address)
{
	x86_kernel_phys_base = phys_address;

	/* Set up initial (temporary) stack */
	asm volatile("movq %0, %%rsp" : : "r" (stack + sizeof(stack)));

	/* Map the host memory to do communication between the host */
	init_smpt();
	setup_boot_param();

	/* Enter the main routine (in the manycore kernel) */
	main();

	/* It can not reach this point. */
	while (1);
}

void arch_ready(void)
{
	sbox_write(SBOX_SCRATCH12, KNF_BOOT_MAGIC_READY);
}

void arch_init(void)
{
	/* Notify the address of the kmsg */
	sbox_write(SBOX_SCRATCH14, (unsigned int)virt_to_phys(&kmsg_buf));
	sbox_write(SBOX_SCRATCH11, AAL_KMSG_SIZE);

	/* Ack boot (trampoline code shall be free'd) */
	sbox_write(SBOX_SCRATCH12, KNF_BOOT_MAGIC_BOOTED);

	init_sfi();

	setup_x86();

	sbox_base = map_fixed_area(SBOX_BASE, SBOX_SIZE, 1);
	boot_param = map_fixed_area((unsigned long)boot_param,
	                            sizeof(*boot_param), 0);

	boot_param->status = 1;

	//asm volatile("spflt %0" : : "r"(0));
}

void arch_set_mikc_queue(void *rq, void *wq)
{
	boot_param->mikc_recv = virt_to_phys(rq);
	boot_param->mikc_send = virt_to_phys(wq);
	sbox_write(SBOX_SCRATCH13, virt_to_phys(wq));
	sbox_write(SBOX_SCRATCH15, virt_to_phys(rq));
	kprintf("MIKC rq: 0x%lX, wq: 0x%lX\n", virt_to_phys(rq), virt_to_phys(wq));
}

int aal_mc_interrupt_host(int cpu, int vector)
{
	unsigned int reg;
	/* Vector is virtual vector, and we use interrupt 0 anyway */
	reg = sbox_read(SBOX_SDBIC0);
	reg |= 1U << 31;
	sbox_write(SBOX_SDBIC0, reg);

	return 0;
}

extern unsigned long sfi_get_memory_address(enum aal_mc_gma_type type, int opt);
extern struct aal_mc_cpu_info *sfi_get_cpu_info(void);

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
	case AAL_MC_NR_RESERVED_AREAS:
	case AAL_MC_RESERVED_AREA_START:
	case AAL_MC_RESERVED_AREA_END:
		return sfi_get_memory_address(type, opt);
	}

	return -ENOENT;
}

struct aal_mc_cpu_info *aal_mc_get_cpu_info(void)
{
	return sfi_get_cpu_info();
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

unsigned long get_transit_page_table(void)
{
	return 0;
}

void arch_delay(int us)
{
	unsigned long tsc;

	/* XXX: 1.2GHz */
	tsc = rdtsc() + 833 * us;
	while (rdtsc() < tsc) {
		cpu_pause();
	}
}

void x86_set_warm_reset(void)
{
	/* outb is not implemented; we do nothing */
}

static unsigned int perf_map_knf[] = 
{
	[APT_TYPE_DATA_PAGE_WALK]                  = CVAL(0x02, 0x00),
	[APT_TYPE_DATA_READ_MISS]                  = CVAL(0x03, 0x00),
	[APT_TYPE_DATA_WRITE_MISS]                 = CVAL(0x04, 0x00),
	[APT_TYPE_BANK_CONFLICTS]                  = CVAL(0x0a, 0x00),

	[APT_TYPE_CODE_CACHE_MISS]                 = CVAL(0x0e, 0x00),
	[APT_TYPE_INSTRUCTIONS_EXECUTED]           = CVAL(0x16, 0x00),
	[APT_TYPE_INSTRUCTIONS_EXECUTED_V_PIPE]    = CVAL(0x17, 0x00),

	[APT_TYPE_L2_READ_MISS]                    = CVAL(0xcb, 0x10),
	[APT_TYPE_L2_CODE_READ_MISS_CACHE_FILL]    = CVAL(0xf0, 0x10),
	[APT_TYPE_L2_DATA_READ_MISS_CACHE_FILL]    = CVAL(0xf1, 0x10),
	[APT_TYPE_L2_CODE_READ_MISS_MEM_FILL]      = CVAL(0xf5, 0x10),
	[APT_TYPE_L2_DATA_READ_MISS_MEM_FILL]      = CVAL(0xf6, 0x10),

	[PERFCTR_MAX_TYPE] = -1,
};

unsigned int *x86_march_perfmap = perf_map_knf;

char *aal_mc_get_kernel_args(void)
{
	return boot_param->kernel_args;
}

void x86_march_perfctr_start(unsigned long counter_mask)
{
	wrmsr(MSR_PERF_FLT_MASK, 0);
}
