#ifndef __HEADER_IHK_BUILTIN_MARCH
#define __HEADER_IHK_BUILTIN_MARCH

#define LAPIC_ID_SHIFT     24
#define LAPIC_ICR_ID_SHIFT 24

extern int perf_counters_discovered;
extern int NUM_PERF_COUNTERS;
extern unsigned long PERF_COUNTERS_MASK;

#define BASE_FIXED_PERF_COUNTERS 32
extern int NUM_FIXED_PERF_COUNTERS;
extern unsigned long FIXED_PERF_COUNTERS_MASK;

#define	MSR_IA32_TIME_STAMP_COUNTER	0x00000010 /* TSC */
#define MSR_IA32_PMC0        0x000000c1
#define MSR_IA32_PERFEVTSEL0 0x00000186
#define MSR_IA32_FIXED_CTR0  0x00000309
#define MSR_PERF_FIXED_CTRL  0x0000038d
#define MSR_PERF_GLOBAL_STATUS       0x0000038e
#define MSR_PERF_GLOBAL_CTRL 0x0000038f
#define MSR_PERF_GLOBAL_OVF_CTRL     0x00000390

#define MSR_OFFCORE_RSP_0       0x000001a6
#define MSR_OFFCORE_RSP_1       0x000001a7

enum extra_reg_type {
	EXTRA_REG_NONE  = -1,   /* not used */

	EXTRA_REG_RSP_0 = 0,    /* offcore_response_0 */
	EXTRA_REG_RSP_1 = 1,    /* offcore_response_1 */
	EXTRA_REG_LBR   = 2,    /* lbr_select */
	EXTRA_REG_LDLAT = 3,    /* ld_lat_threshold */
	EXTRA_REG_FE    = 4,    /* fe_* */

	EXTRA_REG_MAX      /* number of entries needed */
};

#define ENABLE_SSE

#endif
