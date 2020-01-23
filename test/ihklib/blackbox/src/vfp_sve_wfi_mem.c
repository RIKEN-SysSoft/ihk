#define _GNU_SOURCE
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <sched.h>
#include <stdint.h>
#include "mckernel.h"
#include "rdtsc.h"

/* double precision scalar add */
#define fadd10			\
	"fadd d0, d0, d0\n"	\
	"fadd d0, d0, d0\n"	\
	"fadd d0, d0, d0\n"	\
	"fadd d0, d0, d0\n"	\
	"fadd d0, d0, d0\n"	\
	"fadd d0, d0, d0\n"	\
	"fadd d0, d0, d0\n"	\
	"fadd d0, d0, d0\n"	\
	"fadd d0, d0, d0\n"	\
	"fadd d0, d0, d0\n"

#define fadd100 \
	fadd10 fadd10 fadd10 fadd10 fadd10 \
	fadd10 fadd10 fadd10 fadd10 fadd10

#define fadd1000000 do {		\
	int i;				\
					\
	for (i = 0; i < 10000; i++) {	\
		asm volatile(		\
			     fadd100	\
			     :		\
			     :		\
			     :  "q0");	\
	}				\
} while (0)

pthread_barrier_t bar;

/* x = a * b + c
   taken from https://developer.arm.com/docs/100891/0606/coding-considerations/embedding-sve-assembly-code-directly-into-c-and-c-code
 */
unsigned long fmla(double *x, double *a, double *b, double *c, unsigned long n)
{
	unsigned long i;
	unsigned long count;

	asm (
	     "whilelo p0.d, %[i], %[n] \n"
	     "1: \n"
	     "ld1d z0.d, p0/z, [%[a], %[i], lsl #3] \n"
	     "ld1d z1.d, p0/z, [%[b], %[i], lsl #3] \n"
	     "ld1d z2.d, p0/z, [%[c], %[i], lsl #3] \n"
	     "fmla z2.d, p0/m, z0.d, z1.d           \n"
	     "st1d z2.d, p0, [%[x], %[i], lsl #3]   \n"
	     "add %[count], %[count], #1 \n"
	     "uqincd %[i]                           \n"
	     "whilelo p0.d, %[i], %[n]              \n"
	     "b.any 1b"
	     : [i] "=&r" (i),
	       [count] "=&r" (count)
	     : "[i]" (0),
	      "[count]" (0),
	       [x] "r" (x),
	       [a] "r" (a),
	       [b] "r" (b),
	       [c] "r" (c),
	       [n] "r" (n)
	     : "memory", "cc", "p0", "z0", "z1", "z2");

	printf("[ INFO ] sve: loop count: %ld\n", count);
	return i;
}

typedef struct {
#ifdef __AARCH64EB__
	uint16_t next;
	uint16_t owner;
#else /* __AARCH64EB__ */
	uint16_t owner;
	uint16_t next;
#endif /* __AARCH64EB__ */
} __attribute__((aligned(4))) ihk_spinlock_t;

#define ARM64_LSE_ATOMIC_INSN(llsc, lse)	llsc
#define __nops(n)	".rept	" #n "\nnop\n.endr\n"
#define SPIN_LOCK_UNLOCKED	{ 0, 0 }

static void ihk_mc_spinlock_init(ihk_spinlock_t *lock)
{
	*lock = (ihk_spinlock_t)SPIN_LOCK_UNLOCKED;
}

static void __ihk_mc_spinlock_lock_noirq(ihk_spinlock_t *lock)
{
	uint32_t tmp;
	uint32_t lockval, newval;

	asm volatile(
	/* Atomically increment the next ticket. */
	ARM64_LSE_ATOMIC_INSN(
	/* LL/SC */
"	prfm	pstl1strm, %3\n"
"1:	ldaxr	%w0, %3\n"
/*"	add	%w1, %w0, %w5\n"*/
"	add	%w1, %w0, #65536\n"
"	stxr	%w2, %w1, %3\n"
"	cbnz	%w2, 1b\n",
	/* LSE atomics */
"	mov	%w2, #65536\n"
"	ldadda	%w2, %w0, %3\n"
	__nops(3)
	)

	/* Did we get the lock? */
"	eor	%w1, %w0, %w0, ror #16\n"
"	cbz	%w1, 3f\n"
	/*
	 * No: spin on the owner. Send a local event to avoid missing an
	 * unlock before the exclusive load.
	 */
"	sevl\n"
"2:	wfe\n"
"	ldaxrh	%w2, %4\n"
"	eor	%w1, %w2, %w0, lsr #16\n"
"	cbnz	%w1, 2b\n"
	/* We got the lock. Critical section starts here. */
"3:"
	: "=&r" (lockval), "=&r" (newval), "=&r" (tmp), "+Q" (*lock)
	: "Q" (lock->owner)
	: "memory");

	/* Suppress "not used" warning */
	if ((int)tmp < 0 || (int16_t)lockval < 0 ||
	    (int16_t)newval < 0) {
		printf("%s: warning: ticket # has wrapped around\n",
		       __func__);
	}
}

static void __ihk_mc_spinlock_unlock_noirq(ihk_spinlock_t *lock)
{
	uint32_t tmp;

	asm volatile(ARM64_LSE_ATOMIC_INSN(
	/* LL/SC */
	"	ldrh	%w1, %0\n"
	"	add	%w1, %w1, #1\n"
	"	stlrh	%w1, %0",
	/* LSE atomics */
	"	mov	%w1, #1\n"
	"	staddlh	%w1, %0\n"
	__nops(1))
	: "=Q" (lock->owner), "=&r" (tmp)
	:
	: "memory");

	/* Suppress "not used" warning */
	if ((long)tmp < 0) {
		printf("%s: warning: ticket # has wrapped around\n",
		       __func__);
	}
}

ihk_spinlock_t lock;
volatile char tmp;

#define NDOUBLES (1UL << 10)
#define SZSWEEP (1UL << 30)
#define INVARIATNT_TSC_COUNT (100 * 1000 * 1000)

#define MYMMAP(buf, size) do { 			\
	buf = mmap(0, size, \
		 PROT_READ | PROT_WRITE,	     \
		 MAP_ANONYMOUS | MAP_PRIVATE,	     \
		 -1, 0);			     \
	if (buf == MAP_FAILED) { \
		printf("%s: error: allocating memory\n",  \
		       __FILE__); \
		ret = -ENOMEM; \
		goto out; \
	} \
} while (0)

void wfe_handler(int signum)
{
	printf("[ INFO ] signal received, cpu: %d\n",
	       sched_getcpu());
}

void *wfe_child(void *arg)
{
	long start;

	printf("[ INFO ] wfe: child cpu: %d\n",
	       sched_getcpu());

	pthread_barrier_wait(&bar);

	start = rdtsc();
	__ihk_mc_spinlock_lock_noirq(&lock);

	printf("[ INFO ] wfe: woken up after %ld invariant-tsc-cycles\n",
	       rdtsc() - start);

	pthread_exit((void *)0);
}

int wfe(void)
{
	int ret;
	long start;
	int retval;
	pthread_t thr;
	int i;

	printf("[ INFO ] wfe: parent cpu: %d\n",
	       sched_getcpu());
	ihk_mc_spinlock_init(&lock);
	__ihk_mc_spinlock_lock_noirq(&lock);

	pthread_barrier_init(&bar, NULL, 2);

	if ((ret = pthread_create(&thr, NULL, wfe_child, NULL))) {
		int errno_save = errno;

		printf("%s: error: pthread_create faile: %d\n",
		       __func__, errno);
		ret = -errno;
		goto out;
	}

	pthread_barrier_wait(&bar);

	start = rdtsc();
	do {
		for (i = 0; i < 100; i++) {
			asm volatile(nop1000);
		}
	} while (rdtsc() - start < INVARIATNT_TSC_COUNT);

	__ihk_mc_spinlock_unlock_noirq(&lock);

	pthread_join(thr, (void *)&retval);
	printf("[ INFO ] wfe: child exited with %d\n",
	       retval);

	ret = 0;
 out:
	return ret;
}

int main(int argc, char **argv)
{
	int ret, i, opt;
	char *fn_in = NULL, *fn_out = NULL;
	int message = 1;
	int fd_in = -1, fd_out = -1;
	double *x = NULL, *a = NULL, *b = NULL, *c = NULL;
	char *srcbuf = NULL, *dstbuf = NULL;

	while ((opt = getopt(argc, argv, "i:o:s:")) != -1) {
		switch (opt) {
		case 'i':
			fn_in = optarg;
			break;
		case 'o':
			fn_out = optarg;
			break;
		default:
			printf("unknown option %c\n", optopt);
			ret = -EINVAL;
			goto out;
		}
	}

	fd_in = open(fn_in, O_RDWR);
	if (fd_in == -1) {
		int errno_save = errno;

		printf("%s:%d open %s returned %d\n",
		       __FILE__, __LINE__, fn_in, errno);
		ret = -errno_save;
		goto out;
	}

	fd_out = open(fn_out, O_RDWR);
	if (fd_out == -1) {
		int errno_save = errno;

		printf("%s:%d open %s returned %d\n",
		       __FILE__, __LINE__, fn_out, errno);
		ret = -errno_save;
		goto out;
	}

	/* prepare array */

	MYMMAP(x, NDOUBLES * sizeof(double));
	MYMMAP(a, NDOUBLES * sizeof(double));
	MYMMAP(b, NDOUBLES * sizeof(double));
	MYMMAP(c, NDOUBLES * sizeof(double));

	for (i = 0; i < NDOUBLES; i++) {
		x[i] = i;
		a[i] = i + 1;
		b[i] = i + 2;
		c[i] = i + 3;
	}

	MYMMAP(srcbuf, SZSWEEP);
	MYMMAP(dstbuf, SZSWEEP);

	for (i = 0; i < SZSWEEP; i++) {
		srcbuf[i] = i & 255;
		dstbuf[i] = ~(i & 255);
	}

	/* Let parent take stat */
	ret = write(fd_out, &message, sizeof(int));
	if (ret != sizeof(int)) {
		int errno_save = errno;

		printf("%s: write returned %d, errno: %d\n",
		       __FILE__, ret, errno);
		ret = ret >= 0 ? ret : -errno_save;
		goto sync_out;
	}

	/* Wait until parent takes reference stat */
	ret = read(fd_in, &message, sizeof(int));
	if (ret != sizeof(int)) {
		int errno_save = errno;

		printf("%s: read returned %d, errno: %d\n",
		       __FILE__, ret, errno);
		ret = ret >= 0 ? ret : -errno_save;
		goto sync_out;
	}

	/* SVE mutliply-adds */
	printf("[ INFO ] executing sve instructions...\n");
	fmla(x, a, b, c, NDOUBLES);

	/* double precision scalar adds */
	printf("[ INFO ] executing vfp instructions...\n");
	fadd1000000;
	fadd1000000;
	fadd1000000;
	fadd1000000;

	/* wfe stall cycles */
	printf("[ INFO ] executing wfi instructions...\n");
	wfe();

	/* read and write transactions */
	printf("[ INFO ] generating memory bus tx...\n");
	for (i = 0; i < SZSWEEP; i++) {
		dstbuf[i] = tmp;
	}

sync_out:
	/* Let parent take stat */
	ret = write(fd_out, &message, sizeof(int));
	if (ret != sizeof(int)) {
		int errno_save = errno;

		printf("%s: write returned %d, errno: %d\n",
		       __FILE__, ret, errno);
		ret = ret >= 0 ? ret : -errno_save;
		goto out;
	}

	/* Wait until parent takes usage stat */
	ret = read(fd_in, &message, sizeof(int));
	if (ret != sizeof(int)) {
		int errno_save = errno;

		printf("%s: read returned %d, errno: %d\n",
		       __FILE__, ret, errno);
		ret = ret >= 0 ? ret : -errno_save;
		goto out;
	}

	ret = 0;
 out:
	if (x) {
		munmap(x, NDOUBLES * sizeof(double));
	}
	if (a) {
		munmap(a, NDOUBLES * sizeof(double));
	}
	if (b) {
		munmap(b, NDOUBLES * sizeof(double));
	}
	if (c) {
		munmap(c, NDOUBLES * sizeof(double));
	}
	if (srcbuf) {
		munmap(srcbuf, SZSWEEP);
	}
	if (dstbuf) {
		munmap(dstbuf, SZSWEEP);
	}

	return ret;
}
