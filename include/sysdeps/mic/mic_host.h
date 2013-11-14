/**
 * \file include/sysdeps/mic/mic_host.h
 * 
 * \brief 
 * IHK-Host/Manycore for MIC: Boot parameter structure
 *
 * \author Taku Shimosawa  <shimosawa@is.s.u-tokyo.ac.jp> \par
 * Copyright (C) 2011 - 2012  Taku Shimosawa
 */
#ifndef __HEADER_IHK_SYSDEPS_MIC_MIC_HOST
#define __HEADER_IHK_SYSDEPS_MIC_MIC_HOST

/*
 * Boot parameter for MIC (allocated in the host)
 * SCRATCH14 := High address of &mic_boot_param
 * SCRATCH15 := Low address of &mic_boot_param
 */
struct mic_boot_param {
	/* Written by the host */
	char kernel_args[256];

	/* Written by the manycore kernel */
	unsigned long status;
	unsigned long mikc_recv, mikc_send;
	unsigned long mem_size;
	unsigned long cpus;
};

#endif
