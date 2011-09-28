#define PUSH_ALL_REGS     \
        pushq %rax; \
        pushq %rbx; \
        pushq %rcx; \
        pushq %rdx; \
        pushq %rsi; \
        pushq %rdi; \
        pushq %rbp; \
        pushq %r8;  \
        pushq %r9; \
        pushq %r10; \
        pushq %r11; \
        pushq %r12; \
        pushq %r13; \
        pushq %r14; \
        pushq %r15; \
        movq  %ds, %rax; \
        pushq %rax
#define POP_ALL_REGS      \
        popq  %rax; \
        movq  %rax, %ds; \
        popq  %r15; \
        popq  %r14; \
        popq  %r13; \
        popq  %r12; \
        popq  %r11; \
        popq  %r10; \
        popq  %r9;  \
        popq  %r8;  \
        popq  %rbp; \
        popq  %rdi; \
        popq  %rsi; \
        popq  %rdx; \
        popq  %rcx; \
        popq  %rbx; \
        popq  %rax
		
.data
.globl generic_common_handlers
generic_common_handlers:
.text
vector=0
.rept 256
1:      
        cld
		pushq   $vector
        jmp     common_interrupt
.previous
        .quad   1b
.text
vector=vector+1
.endr

common_interrupt:
		PUSH_ALL_REGS
		movq    128(%rsp), %rdi
		movq	%rsp, %rsi
		call 	handle_interrupt /* Enter C code */
		POP_ALL_REGS
		addq	$8, %rsp
		iretq

.globl __page_fault_handler_address
__page_fault_handler_address:
		.quad	0

.globl page_fault
page_fault:
		cld
		PUSH_ALL_REGS
		movq	%cr2, %rdi
		movq	%rsp, %rsi
		movq	__page_fault_handler_address(%rip), %rax
		andq	%rax, %rax
		jz		1f
		call	*%rax
		POP_ALL_REGS
		addq	$8, %rsp
		iretq
1:		
		jmp		1b

.globl general_protection_exception
general_protection_exception:
		cld
		PUSH_ALL_REGS
		movq	%rsp, %rdi
		call	gpe_handler
		POP_ALL_REGS
		addq	$8, %rsp
		iretq

	