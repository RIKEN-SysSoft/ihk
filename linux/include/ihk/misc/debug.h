/**
 * \file host/include/ihk/misc/debug.h
 * \brief
 *	IHK-Host: Debug printf functions
 * \author Taku Shimosawa  <shimosawa@is.s.u-tokyo.ac.jp> \par
 *	 Copyright (C) 2011-2012 Taku Shimosawa <shimosawa@is.s.u-tokyo.ac.jp>
 */
#ifndef __HEADER_IHK_MISC_DEBUG_H
#define __HEADER_IHK_MISC_DEBUG_H

#if defined(IHK_DEBUG) || defined(CONFIG_IHK_DEBUG)
#ifdef __KERNEL__
#define dprintf(...)         printk(__VA_ARGS__)
#else
#define dprintf(...)         printf(__VA_ARGS__)
#endif /* KERNEL */
#else
#define dprintf(...)         do { } while(0)
#endif /* IHK_DEBUG || CONFIG_IHK_DEBUG */

#define tprintf(format, ...) dprintf("%s Line %d : " format, __FILE__,  \
                                     __LINE__, __VA_ARGS__)
#define dprint_var_i4(var)   dprintf(#var " = %d\n", var)
#define dprint_var_i8(var)   dprintf(#var " = %ld\n", var)
#define dprint_var_x4(var)   dprintf(#var " = %x\n", var)
#define dprint_var_x8(var)   dprintf(#var " = %lx\n", var)
#define dprint_var_p(var)    dprintf(#var " = %p\n", var)

#define dprint_func_enter    dprintf("==> %s\n", __FUNCTION__);
#define dprint_func_leave    dprintf("<== %s\n", __FUNCTION__);

#endif
