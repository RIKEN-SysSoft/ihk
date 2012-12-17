/**
 * \file knf_user.h
 * \brief AAL KNF Driver: Definitions of KNF-specific ioctl constants
 */
#ifndef __KNF_USER_H
#define __KNF_USER_H

#include <aal/aal_host_user.h>

#define KNF_DEBUG_READ_SCRATCH  (AAL_DEVICE_DEBUG_START + 0)
#define KNF_DEBUG_READ_SBOX     (AAL_DEVICE_DEBUG_START + 1)
#define KNF_DEBUG_DMA_TEST      (AAL_DEVICE_DEBUG_START + 10)

#endif
