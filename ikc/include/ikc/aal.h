#ifndef HEADER_AAL_IKC_AAL_H
#define HEADER_AAL_IKC_AAL_H

/* Support for both manycore and host side */
#ifdef AAL_OS_MANYCORE
#define aal_ikc_spinlock_lock    aal_mc_spinlock_lock
#define aal_ikc_spinlock_unlock  aal_mc_spinlock_unlock
#define aal_ikc_spinlock_init    aal_mc_spinlock_init
#endif

#endif
