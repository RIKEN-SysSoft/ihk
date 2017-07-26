/**
 * \file ihk_os_status.h
 * \brief
 *	IHK-Master: OS status
 * \author Taku Shimosawa  <shimosawa@is.s.u-tokyo.ac.jp> \par
 *	Copyright (C) 2011-2012 Taku Shimosawa <shimosawa@is.s.u-tokyo.ac.jp>
 */
#ifndef IHK_OS_STATUS_H_INCLUDED
#define IHK_OS_STATUS_H_INCLUDED

/** \brief Status of a manycore kernel instance */
enum ihk_os_status {
	IHK_OS_STATUS_NOT_BOOTED,
	IHK_OS_STATUS_BOOTING,
	IHK_OS_STATUS_BOOTED,    /* OS booted and acked */
	IHK_OS_STATUS_READY,     /* OS is ready and fully functional */
	IHK_OS_STATUS_FREEZING,  /* OS is freezing */
	IHK_OS_STATUS_FROZEN,    /* OS is frozen */
	IHK_OS_STATUS_SHUTDOWN,  /* OS is shutting down */
	IHK_OS_STATUS_STOPPED,   /* OS stopped successfully */
	IHK_OS_STATUS_FAILED,    /* OS panics or failed to boot */
	IHK_OS_STATUS_HUNGUP,    /* OS is hungup */
};
#endif /* !defined(IHK_OS_STATUS_H_INCLUDED) */
