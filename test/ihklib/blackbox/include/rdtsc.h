/* rdtsc.h COPYRIGHT FUJITSU LIMITED 2016 */
#ifndef __HEADER_ARM64_IHK_RDTSC_H
#define __HEADER_ARM64_IHK_RDTSC_H

#define isb()	asm volatile("isb" : : : "memory")

/* @ref.impl linux-linaro/arch/arm64/include/asm/arch_timer.h::arch_counter_get_cntvct */
static inline unsigned long rdtsc(void)
{
	unsigned long cval;

	isb();
	asm volatile("mrs %0, cntvct_el0" : "=r" (cval));

	return cval;
}

#endif	/* __HEADER_ARM64_IHK_RDTSC_H */
