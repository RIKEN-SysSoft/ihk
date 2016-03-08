#ifndef __RDTSC_H
#define __RDTSC_H
static inline unsigned long
rdtsc()
{
	unsigned int low;
	unsigned int high;

	asm volatile("rdtsc" : "=a"(low), "=d"(high));
	return ((unsigned long)high) << 32 | low;
}
#endif
