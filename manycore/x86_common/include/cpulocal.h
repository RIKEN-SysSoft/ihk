#ifndef HEADER_X86_COMMON_CPULOCAL_H
#define HEADER_X86_COMMON_CPULOCAL_H

#include <types.h>
#include <registers.h>

/*
 * CPU Local Page
 * 0 -    : struct x86_cpu_local_varibles
 * - 4096 : kernel stack
 */

struct x86_cpu_local_variables {
/* 0 */
	unsigned long processor_id;

	unsigned long apic_id;

/* 16 */
	struct x86_desc_ptr gdt_ptr;
	unsigned short pad1[3];

/* 32 */
	uint64_t gdt[10];
/* 112 */
	struct tss64 tss;

} __attribute__((packed));



#endif
