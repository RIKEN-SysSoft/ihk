/**
 * \file ihk_debug.h
 * \brief
 *	IHK-Master: OS status
 * \author Taku Shimosawa  <shimosawa@is.s.u-tokyo.ac.jp> \par
 *	Copyright (C) 2011-2012 Taku Shimosawa <shimosawa@is.s.u-tokyo.ac.jp>
 */
#ifndef IHK_DEBUG_H_INCLUDED
#define IHK_DEBUG_H_INCLUDED

/* careful not to exceed the max order of 10 (= 4MiB) in RHEL-7 (x86_64) */
#define IHK_KMSG_SIZE            ((4 << 20) - 4096)

#ifdef ENABLE_KMSG_REDIRECT
#define IHK_KMSG_HIGH_WATER_MARK 1
#define IHK_KMSG_NOTIFY_DELAY    400
#else
#define IHK_KMSG_HIGH_WATER_MARK (IHK_KMSG_SIZE / 2)
#define IHK_KMSG_NOTIFY_DELAY    400 /* Unit is us, 400 us would avoid overloading fwrite of ihkmond */
#endif

struct ihk_kmsg_buf {
	int lock; /* Be careful, it's inter-kernel lock */
	int tail;
	int len;
	int head;
	char padding[4096 - sizeof(int) * 4]; /* Alignmment needed for some systems */
	char str[IHK_KMSG_SIZE];
};

#endif /* !defined(IHK_DEBUG_H_INCLUDED) */
