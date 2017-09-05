/* smp-arm64-startup.S COPYRIGHT FUJITSU LIMITED 2015 */
/**
 * \file startup.S
 * \brief
 *	IHK BUILTIN Driver: startup routine
 * \author Taku Shimosawa  <shimosawa@is.s.u-tokyo.ac.jp> \par
 * Copyright (C) 2011-2012 Taku Shimosawa <shimosawa@is.s.u-tokyo.ac.jp>
 */

.globl startup_data
startup_data:
	b	startup_body
base:
	.org	16
pgtbl:
	.quad	0
stack:
	.quad	0
arg2:
	.quad	0
trampoline:
	.quad	0
next_pc:
	.quad	0
startup_body:
	adr	x6, startup_data	/* get startup_data PA */
	str	x6, [x5]		/* store startup_data PA */
	ldr	x27, next_pc
	br	x27 /* jump to McKernel. */
	b	.
.globl startup_data_end
startup_data_end:
