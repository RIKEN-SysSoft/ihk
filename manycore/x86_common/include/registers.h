#ifndef __HEADER_X86_COMMON_REGISTERS_H
#define __HEADER_X86_COMMON_REGISTERS_H

#include <types.h>

#define MSR_FS_BASE    0xc0000100
#define MSR_GS_BASE    0xc0000101

#define MSR_IA32_APIC_BASE 0x000000001b

/* AMD */
#define MSR_PERF_CTL_0 0xc0010000
#define MSR_PERF_CTR_0 0xc0010004

static void wrmsr(unsigned int idx, unsigned long value){
	unsigned int high, low;

	high = value >> 32;
	low = value & 0xffffffffU;

	asm volatile("wrmsr" : : "c" (idx), "a" (low), "d" (high) : "memory");
}

static unsigned long rdpmc(unsigned int counter)
{
	unsigned int high, low;

	asm volatile("rdpmc" : "=a" (low), "=d" (high) : "c" (counter));

	return (unsigned long)high << 32 | low;
}

static unsigned long rdmsr(unsigned int index)
{
	unsigned int high, low;

	asm volatile("rdmsr" : "=a" (low), "=d" (high) : "c" (index));

	return (unsigned long)high << 32 | low;
}

static unsigned long rdtsc(void)
{
	unsigned int high, low;

	asm volatile("rdtsc" : "=a" (low), "=d" (high));

	return (unsigned long)high << 32 | low;
}

static void set_perfctl(int counter, int event, int mask)
{
	unsigned long value;

	value = ((unsigned long)(event & 0x700) << 32)
		| (event & 0xff) | ((mask & 0xff) << 8) | (1 << 18)
		 | (1 << 17);

	wrmsr(MSR_PERF_CTL_0 + counter, value);
}

static void start_perfctr(int counter)
{
	unsigned long value;

	value = rdmsr(MSR_PERF_CTL_0 + counter);
	value |= (1 << 22);
	wrmsr(MSR_PERF_CTL_0 + counter, value);
}
static void stop_perfctr(int counter)
{
	unsigned long value;

	value = rdmsr(MSR_PERF_CTL_0 + counter);
	value &= ~(1 << 22);
	wrmsr(MSR_PERF_CTL_0 + counter, value);
}

static void clear_perfctl(int counter)
{
	wrmsr(MSR_PERF_CTL_0 + counter, 0);
}

static void set_perfctr(int counter, unsigned long value)
{
	wrmsr(MSR_PERF_CTR_0 + counter, value);
}

static unsigned long read_perfctr(int counter)
{
	return rdpmc(counter);
}

struct x86_desc_ptr {
        uint16_t size;
        uint64_t address;
} __attribute__((packed));

struct tss64 {
        unsigned int reserved0;
        unsigned long rsp0;
        unsigned long rsp1;
        unsigned long rsp2;
        unsigned int reserved1, reserved2;
        unsigned long ist[7];
        unsigned int reserved3, reserved4;
        unsigned short reserved5;
        unsigned short iomap_address;
} __attribute__((packed));

#endif
