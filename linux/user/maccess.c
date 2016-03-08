#include <stdio.h>
#include <stdlib.h>
#include <ihk/rdtsc.h>

#if 0
static unsigned long rdtsc(void)
{
	unsigned int low, high;

	asm volatile("rdtsc" : "=a"(low), "=d"(high));
 
	return (low | ((unsigned long)high << 32));
}
#endif

#define rdtscll(v) ( v = rdtsc() )

#define LEN  256 * 1024 * 1024

static void measure_time(char *p)
{
	unsigned long st, ed, m = 0;
	int j;

	p = (char *)(((unsigned long)p + 4095) & ~(4095UL));
	printf("%p\n", p);
	rdtscll(st);
	for (j = 0; j < LEN; j += 32) {
//		*(volatile unsigned long *)((char *)p + j) = 0x29;
		m += *(volatile unsigned long *)((char *)p + j);
	}
	asm volatile("");
	rdtscll(ed);
	printf("Time = %ld, %lx\n", ed - st, m);
}

int main(void)
{
	int i;
	char *p;

	p = malloc(LEN + 4096);
	for (i = 0; i < LEN + 4096; i++) {
		p[i] = 0;
	}
	
	measure_time(p);

	return 1;
}
