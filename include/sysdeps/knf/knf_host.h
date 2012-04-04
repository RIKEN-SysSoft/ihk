/**
 * \file include/sysdeps/knf/knf_host.h
 * \brief AAL-Host/Manycore for KNF: Boot parameter structure
 *
 * Copyright (C) 2011-2012 Taku Shimosawa <shimosawa@is.s.u-tokyo.ac.jp>
 */
#ifndef __HEADER_AAL_SYSDEPS_KNF_KNF_HOST
#define __HEADER_AAL_SYSDEPS_KNF_KNF_HOST

/*
 * Boot parameter for KNF (allocated in the host)
 * SCRATCH14 := High address of &knf_boot_param
 * SCRATCH15 := Low address of &knf_boot_param
 */
struct knf_boot_param {
	/* Written by the host */
	char kernel_args[256];

	/* Written by the manycore kernel */
	unsigned long status;
	unsigned long mikc_recv, mikc_send;
	unsigned long mem_size;
	unsigned long cpus;
};

#endif
