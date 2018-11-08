#ifndef ARCH_RUSAGE_H_INCLUDED
#define ARCH_RUSAGE_H_INCLUDED

#include "config.h"


#define IHK_NUM_PAGESIZES 3

// same as PTLx_SHIFT definitions
#define _SZ4KB  (1UL<<12)
#define _SZ16KB (1UL<<14)
#define _SZ64KB (1UL<<16)

#ifdef CONFIG_ARM64_64K_PAGES
# define GRANULE_SIZE _SZ64KB
#else
# define GRANULE_SIZE _SZ4KB
#endif

const int ihk_pgsizes[] = {
#if GRANULE_SIZE == _SZ4KB
	1ULL << 12,
	1ULL << 21,
	1ULL << 30,
#elif GRANULE_SIZE == _SZ16KB
	1ULL << 14,
	1ULL << 25,
	1ULL << 36,
#elif GRANULE_SIZE == _SZ64KB
	1ULL << 16,
	1ULL << 29,
	1ULL << 42,
#else
# error granule size error.
#endif
};

#endif /* !defined(ARCH_RUSAGE_H_INCLUDED) */
