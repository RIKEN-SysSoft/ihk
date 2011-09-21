#ifndef MICCONST_H
#define MICCONST_H

#define SBOX_BASE           0x08007D0000ULL     /* PCIE Box Registers */

#define MIC_GTT_BASE        0x0800800000ULL
//#define MIC_GTT_BASE        0x007FFC0000ULL

#define SBOX_SICE0_DBR(x)       ((x) & 0xf)
#define SBOX_SICE0_DBR_BITS(x)      ((x) & 0xf)
#define SBOX_SICE0_DMA(x)       (((x) >> 8) & 0xff)
#define SBOX_SICE0_DMA_BITS(x)      (((x) & 0xff) << 8)

#define MIC_DBR_ALL_MASK            0xf
#define MIC_DMA_ALL_MASK            0xff

/* /host/driver/uos_download.c */
#define MIC_DMA_INTERRUPT_VECTOR 229
#define MIC_ICR_INTVEC_SHIFT 0

#endif /* MICCONST_H */
