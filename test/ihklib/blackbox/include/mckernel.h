#ifndef __MCKERNEL_H_INCLUDED__
#define __MCKERNEL_H_INCLUDED__

#define nop10 \
	"nop; nop; nop; nop; nop;" \
	"nop; nop; nop; nop; nop;"

#define nop100 \
	nop10 nop10 nop10 nop10 nop10 \
	nop10 nop10 nop10 nop10 nop10

#define nop1000 \
	nop100 nop100 nop100 nop100 nop100 \
	nop100 nop100 nop100 nop100 nop100

#define nop1000000 do {			\
	int i;				\
					\
	for (i = 0; i < 1000; i++) {	\
		asm volatile(nop1000);	\
	}				\
} while (0)

extern unsigned int ldr_src;
extern unsigned int ldr_dst;

#define ldr10 \
	"ldr %w0, %1\n"	\
	"ldr %w0, %1\n"	\
	"ldr %w0, %1\n"	\
	"ldr %w0, %1\n"	\
	"ldr %w0, %1\n"	\
	"ldr %w0, %1\n"	\
	"ldr %w0, %1\n"	\
	"ldr %w0, %1\n"	\
	"ldr %w0, %1\n"	\
	"ldr %w0, %1\n"

#define ldr100 \
	ldr10 ldr10 ldr10 ldr10 ldr10 \
	ldr10 ldr10 ldr10 ldr10 ldr10

#define ldr1000 \
	ldr100 ldr100 ldr100 ldr100 ldr100 \
	ldr100 ldr100 ldr100 ldr100 ldr100

#define ldr1000000 do {				\
	int i;					\
						\
	for (i = 0; i < 1000; i++) {		\
		asm volatile(			\
			     ldr1000		\
			     :  "=r" (ldr_dst)	\
			     :  "Q" (ldr_src)	\
			     :  );		\
	}					\
} while (0)

#endif
