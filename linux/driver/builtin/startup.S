/**
 * \file startup.S
 * \brief
 *	IHK BUILTIN Driver: startup routine
 * \author Taku Shimosawa  <shimosawa@is.s.u-tokyo.ac.jp> \par
 * Copyright (C) 2011-2012 Taku Shimosawa <shimosawa@is.s.u-tokyo.ac.jp>
 */

.globl startup_data
startup_data:
	leaq	0(%rip),%rbx
base:
	jmp	startup_body
	.org	16
pgtbl:
	.quad	0
stack:
	.quad	0
arg2:
	.quad	0
next_rip:
	.quad	0
startup_body:
	movq	%cr3,%rax
	movq	%rax,%cr3

	movq	(pgtbl - base)(%rbx),%rax
	movq	%rax,%cr3

	movq	%cr3,%rax
	movq	%rax,%cr3

	movq	(stack - base)(%rbx),%rax
	movq	%rax,%rsp

	movq	%cr3,%rax
	movq	%rax,%cr3

	movq	(arg2 - base)(%rbx),%rsi
	movq	(next_rip - base)(%rbx),%rcx

	movq	%cr3,%rax
	movq	%rax,%cr3

	jmp	*%rcx
.globl startup_data_end
startup_data_end:
