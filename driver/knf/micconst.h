#ifndef MICCONST_H
#define MICCONST_H

/* /host/driver/mic_common.h */
#define DLDR_APT_BAR 0
#define DLDR_MMIO_BAR 4

#define MMIO_DBOX_BASE_OFFSET       0x00000000
#define MMIO_SBOX_BASE_OFFSET       0x00010000
#define MMIO_GTT_BASE_OFFSET        0x00040000

#define SCRATCH2_DOWNLOAD_ADDR(x)   ((x) & 0xfffff000)
#define SCRATCH2_DOWNLOAD_STATUS(x) ((x) & 0x1)
#define SCRATCH2_APIC_ID(x)     (((x) >> 1) & 0x1ff)

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
