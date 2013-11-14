/**
 * \file include/sysdeps/mic/mic_dma.h
 * 
 * \brief 
 * IHK-Host/Manycore for MIC: DMA queue element structures
 * taken from MPSS Linux
 *
 * \author Taku Shimosawa  <shimosawa@is.s.u-tokyo.ac.jp> \par
 * Copyright (C) 2011 - 2012  Taku Shimosawa
 */
#ifndef HEADER_MIC_MIC_DMA_H
#define HEADER_MIC_MIC_DMA_H

#include "mic_type.h"

union md_mic_dma_desc {
    union {
        struct {
            uint64_t rsvd0;
            uint64_t rsvd1:60;
            uint64_t type:4;
        } nop;
        struct {
            uint64_t sap:40;
            uint64_t index:3;
            uint64_t rsvd0:3;
            uint64_t length:14;
            uint64_t rsvd1:4;
            uint64_t dap:40;
            uint64_t resd:15;
            uint64_t twb:1;
            uint64_t intr:1;
            uint64_t c:1;
            uint64_t co:1;
            uint64_t ecy:1;
            uint64_t type:4;
        } memcpy;
        struct {
            uint64_t data;
            uint64_t dap:40;
            uint64_t rsvdr0:19;
            uint64_t intr:1;
            uint64_t type:4;
        } status;
        struct {
            uint64_t data:32;
            uint64_t rsvd0:32;
            uint64_t dap:40;
            uint64_t rsvd1:20;
            uint64_t type:4;
        } general;
        struct {
            uint64_t data;
            uint64_t rsvd0:53;
            uint64_t cs:1;
            uint64_t index:3;
            uint64_t h:1;
            uint64_t sel:2;
            uint64_t type:4;
        } knc;
        struct {
            uint64_t skap:40;
            uint64_t ski:3;
            uint64_t rsvd0:21;
            uint64_t rsvd1:51;
            uint64_t di:3;
            uint64_t rsvd2:6;
            uint64_t type:4;
        } key;
    } desc;
    struct {
        uint64_t qw0;
        uint64_t qw1;
    } qwords;
};

/* host/driver/include/mic/mic_sbox_md.h */
#define SET_SBOX_DRARHI_BA(addr)    ((addr) & 0xf)
#define SET_SBOX_DRARHI_SIZE(size)  (((size) & 0x1ffff) << 4)
#define SET_SBOX_DRARHI_PAGE(page)  (((page) & 0x1f) << 21)
#define SET_SBOX_DRARHI_SYS(bit)    (((bit) & 0x1) << 26)

#endif
