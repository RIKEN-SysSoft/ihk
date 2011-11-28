#ifndef HEADER_GENERIC_AAL_PERFCTR_H
#define HEADER_GENERIC_AAL_PERFCTR_H

#define PERFCTR_USER_MODE   0x01
#define PERFCTR_KERNEL_MODE 0x02

enum aal_perfctr_type {
	APT_TYPE_INSTRUCTIONS,
	APT_TYPE_L1D_MISS,
	APT_TYPE_L1I_MISS,
	APT_TYPE_L1_MISS,
	APT_TYPE_L2_MISS,
	APT_TYPE_LLC_MISS,
	APT_TYPE_STALL,
	PERFCTR_MAX_TYPE,
};

int aal_mc_perfctr_init(int counter, enum aal_perfctr_type type, int mode);
int aal_mc_perfctr_start(unsigned long counter_mask);
int aal_mc_perfctr_stop(unsigned long counter_mask);
int aal_mc_perfctr_reset(int counter);
int aal_mc_perfctr_read_mask(unsigned long counter_mask, unsigned long *value);
unsigned long aal_mc_perfctr_read(int counter);

#endif

