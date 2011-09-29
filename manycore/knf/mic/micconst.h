#ifndef MICCONST_H
#define MICCONST_H

#define SBOX_BASE           0x08007D0000ULL     /* PCIE Box Registers */
#define SBOX_SIZE           0x30000ULL
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

/* /card/driver/include/mic/micscif_smpt.h */
#define SNOOP_ON  (0 << 0)
#define SNOOP_OFF (1 << 0)
#define NUM_SMPT_REGISTERS 32
#define SMPT_MASK 0x1F
#define MIC_SYSTEM_PAGE_SHIFT 34ULL
#define MIC_SYSTEM_PAGE_MASK ((1ULL << MIC_SYSTEM_PAGE_SHIFT) - 1ULL)
#define BUILD_SMPT(NO_SNOOP, HOST_ADDR)  \
	(uint32_t)(((((HOST_ADDR)<< 2) & (~0x03)) | ((NO_SNOOP) & (0x01))))

#define MIC_SYSTEM_BASE     0x8000000000ULL

#endif /* MICCONST_H */
