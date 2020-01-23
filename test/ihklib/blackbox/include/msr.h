#ifndef __MSR_H_INCLUDED__
#define __MSR_H_INCLUDED__

#define Op0_shift	19
#define Op0_mask	0x3
#define Op1_shift	16
#define Op1_mask	0x7
#define CRn_shift	12
#define CRn_mask	0xf
#define CRm_shift	8
#define CRm_mask	0xf
#define Op2_shift	5
#define Op2_mask	0x7

#define sys_reg(op0, op1, crn, crm, op2)	       \
	(((op0) << Op0_shift) | ((op1) << Op1_shift) | \
	 ((crn) << CRn_shift) | ((crm) << CRm_shift) | \
	 ((op2) << Op2_shift))

#define IMP_FJ_TAG_ADDRESS_CTRL_EL1		sys_reg(3, 0, 11, 2, 0)
#define IMP_SCCR_CTRL_EL1			sys_reg(3, 0, 11, 8, 0)
#define IMP_SCCR_ASSIGN_EL1			sys_reg(3, 0, 11, 8, 1)
#define IMP_SCCR_SET0_L2_EL1			sys_reg(3, 0, 15, 8, 2)
#define IMP_SCCR_SET1_L2_EL1			sys_reg(3, 0, 15, 8, 3)
#define IMP_SCCR_L1_EL0				sys_reg(3, 3, 11, 8, 2)
#define IMP_PF_CTRL_EL1				sys_reg(3, 0, 11, 4, 0)
#define IMP_PF_STREAM_DETECT_CTRL_EL0		sys_reg(3, 3, 11, 4, 0)
#define IMP_PF_INJECTION_CTRL0_EL0		sys_reg(3, 3, 11, 6, 0)
#define IMP_BARRIER_BST_BIT_EL1			sys_reg(3, 0, 11, 12, 4)

#endif
