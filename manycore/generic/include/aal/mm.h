#ifndef __HEADER_GENERIC_AAL_MM_H
#define __HEADER_GENERIC_AAL_MM_H

#include <memory.h>

enum aal_mc_gma_type {
	AAL_MC_GMA_MAP_START,
	AAL_MC_GMA_MAP_END,
	AAL_MC_GMA_AVAIL_START,
	AAL_MC_GMA_AVAIL_END,
	AAL_MC_GMA_HEAP_START,
};

enum aal_mc_ma_type {
	AAL_MC_MA_AVAILABLE,
	AAL_MC_MA_RESERVED,
	AAL_MC_MA_SPECIAL,
};

enum aal_mc_ap_flag {
	AAL_MC_AP_FLAG,
};

struct aal_mc_memory_area {
	unsigned long start;
	unsigned long size;
	enum aal_mc_ma_type type;
};

struct aal_mc_memory_node {
	int node;
	int nareas;
	struct aal_mc_memory_area *areas;
};

unsigned long aal_mc_get_memory_address(enum aal_mc_gma_type, int);

void aal_mc_reserve_arch_pages(unsigned long start, unsigned long end,
                               void (*cb)(unsigned long, unsigned long, int));

struct aal_mc_pa_ops {
	void *(*alloc)(enum aal_mc_ap_flag);
	void (*free)(void *);
};

void aal_mc_set_page_allocator(struct aal_mc_pa_ops *);
void aal_mc_set_page_fault_handler(void (*h)(unsigned long, void *));

#endif
