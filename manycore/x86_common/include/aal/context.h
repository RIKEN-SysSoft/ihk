#ifndef __HEADER_X86_COMMON_CONTEXT_H
#define __HEADER_X86_COMMON_CONTEXT_H

#include <registers.h>

struct x86_kregs {
	unsigned long rsp, rbp, rbx, rsi, rdi, r12, r13, r14, r15, rflags;
};

typedef struct x86_kregs aal_mc_kernel_context_t;
/* XXX: User context should contain floating point registers */
typedef struct x86_regs aal_mc_user_context_t;

#endif
