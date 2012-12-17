.text
.globl builtin_dma_intr_enter
builtin_dma_intr_enter:
		cld
		pushq $0xf2
        pushq %rax
        pushq %rbx
        pushq %rcx
        pushq %rdx
        pushq %rsi
        pushq %rdi
        pushq %rbp
        pushq %r8
        pushq %r9
        pushq %r10
        pushq %r11
        pushq %r12
        pushq %r13
        pushq %r14
        pushq %r15
        movq  %ds, %rax
        pushq %rax
		movw  %ss, %ax
		movw  %ax, %ds
		
		call shimos_dma_interrupt_handler

		popq  %rax
        movq  %rax, %ds
        popq  %r15
        popq  %r14
        popq  %r13
        popq  %r12
        popq  %r11
        popq  %r10
        popq  %r9
        popq  %r8
        popq  %rbp
        popq  %rdi
        popq  %rsi
        popq  %rdx
        popq  %rcx
        popq  %rbx
        popq  %rax
		addq  $8, %rsp
		iretq
