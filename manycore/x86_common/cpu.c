#include <aal/cpu.h>
#include <aal/debug.h>
#include <types.h>
#include <errno.h>
#include <list.h>
#include <memory.h>
#include <string.h>
#include <registers.h>

#define LAPIC_ID            0x020
#define LAPIC_TIMER         0x320
#define LAPIC_TIMER_INITIAL 0x380
#define LAPIC_TIMER_CURRENT 0x390
#define LAPIC_TIMER_DIVIDE  0x3e0
#define LAPIC_SPURIOUS      0x0f0
#define LAPIC_EOI           0x0b0
#define LAPIC_ICR0          0x300
#define LAPIC_ICR2          0x310

struct x86_regs{
        unsigned long ds, r15, r14, r13, r12, r11, r10, r9, r8;
        unsigned long rbp, rdi, rsi, rdx, rcx, rbx, rax;
	unsigned long error, rip, cs, rflags, rsp, ss;
};

extern int kprintf(const char *format, ...);

static struct idt_entry{
        uint32_t desc[4];
} idt[256] __attribute__((aligned(16)));

static struct desc_ptr{
        uint16_t size;
        uint64_t address;
} __attribute__((packed)) idt_desc, gdt_desc;

static uint64_t gdt[] __attribute__((aligned(16))) = {
	0,                  /* 0 */
	0,                  /* 8 */
	0,                  /* 16 */
	0,                  /* 24 */
	0x00af9b000000ffff, /* 32 : KERNEL_CS */
	0x00cf93000000ffff, /* 40 : KERNEL_DS */
	0x00affb000000ffff, /* 48 : USER_CS */
	0x00aff3000000ffff, /* 56 : USER_DS */
	0x0000890000000067, /* 64 : TSS */
	0,                  /* (72: TSS) */
};

struct tss64{
        unsigned int reserved0;
        unsigned long rsp0;
        unsigned long rsp1;
        unsigned long rsp2;
        unsigned int reserved1, reserved2;
        unsigned long ist[7];
        unsigned int reserved3, reserved4;
        unsigned short reserved5;
        unsigned short iomap_address;
} __attribute__((packed));

struct tss64 tss __attribute__((aligned(16)));

static void set_idt_entry(int idx, unsigned long addr)
{
        idt[idx].desc[0] = (addr & 0xffff) | (KERNEL_CS << 16);
        idt[idx].desc[1] = (addr & 0xffff0000) | 0x8e00;
        idt[idx].desc[2] = (addr >> 32);
        idt[idx].desc[3] = 0;
}

static void set_idt_entry_trap_gate(int idx, unsigned long addr)
{
        idt[idx].desc[0] = (addr & 0xffff) | (KERNEL_CS << 16);
        idt[idx].desc[1] = (addr & 0xffff0000) | 0xef00;
        idt[idx].desc[2] = (addr >> 32);
        idt[idx].desc[3] = 0;
}

extern uint64_t generic_common_handlers[];

void reload_idt(void)
{
	asm volatile("lidt %0" : : "m"(idt_desc) : "memory");
}

static struct list_head handlers[256 - 32];
extern char page_fault[], general_protection_exception[];

static void init_idt(void)
{
	int i;

	idt_desc.size = sizeof(idt) - 1;
        idt_desc.address = (unsigned long)idt;
        
        for (i = 0; i < 256; i++) {
	        if (i >= 32) {
		        INIT_LIST_HEAD(&handlers[i - 32]);
	        }
	        set_idt_entry(i, generic_common_handlers[i]);
        }

        set_idt_entry(13, (unsigned long)general_protection_exception);
        set_idt_entry(14, (unsigned long)page_fault);

        reload_idt();
}

void init_fpu(void)
{
	unsigned long reg;

	asm volatile("movq %%cr0, %0" : "=r"(reg));
	/* Unset EM and TS flag. */
	reg &= ~((1 << 2) | (1 << 3));
	asm volatile("movq %0, %%cr0" : : "r"(reg));

#ifdef ENABLE_SSE
	asm volatile("movq %%cr4, %0" : "=r"(reg));
	/* Set OSFXSR flag. */
	reg |= (1 << 9);
	asm volatile("movq %0, %%cr4" : : "r"(reg));
#endif

	asm volatile("finit");
}

void init_gdt(void)
{
        register unsigned long stack_pointer asm("rsp");
        unsigned long tss_addr = (unsigned long)&tss;

        memset(&tss, 0, sizeof(tss));
        tss.rsp0 = stack_pointer;
        
        /* 0x89 = Present (8) | Type = 9 (TSS) */
        gdt[GLOBAL_TSS_ENTRY] = (sizeof(tss) - 1) 
	        | ((tss_addr & 0xffffff) << 16)
	        | (0x89UL << 40) | ((tss_addr & 0xff000000) << 32);
        gdt[GLOBAL_TSS_ENTRY + 1] = (tss_addr >> 32);

        gdt_desc.size = sizeof(gdt) - 1;
        gdt_desc.address = (unsigned long)gdt;
        
        /* Load the new GDT, and set up CS, DS and SS. */
        asm volatile("pushq %1\n"
                     "leaq 1f(%%rip), %%rbx\n"
                     "pushq %%rbx\n"
                     "lgdt %0\n"
                     "lretq\n"
                     "1:\n" : :
                     "m" (gdt_desc),
                     "i" (KERNEL_CS) : "rbx");
        asm volatile("movl %0, %%ds" : : "r"(KERNEL_DS));
        asm volatile("movl %0, %%ss" : : "r"(KERNEL_DS));
        /* And, set TSS */
        asm volatile("ltr %0" : : "r"((short)GLOBAL_TSS) : "memory");
}

static void *lapic_vp;
void lapic_write(int reg, unsigned int value)
{
	*(volatile unsigned int *)((char *)lapic_vp + reg) = value;
}

unsigned int lapic_read(int reg)
{
	return *(volatile unsigned int *)((char *)lapic_vp + reg);
}

void init_lapic(void)
{
        unsigned long baseaddr;

        /* Enable Local APIC */
        baseaddr = rdmsr(MSR_IA32_APIC_BASE);
        lapic_vp = map_fixed_area(baseaddr & PAGE_MASK, PAGE_SIZE, 1);
        kprintf("Local APIC Base = %lx (%p)\n", baseaddr, lapic_vp);

        baseaddr |= 0x800;
        wrmsr(MSR_IA32_APIC_BASE, baseaddr);

        kprintf("Local APIC ID = %lx\n",  lapic_read(LAPIC_ID));
        lapic_write(LAPIC_SPURIOUS, 0x1ff);
}

void lapic_ack(void)
{
        lapic_write(LAPIC_EOI, 0);
}

extern void init_page_table(void);

void setup_x86(void)
{
	cpu_disable_interrupt();

	kprintf("init_idt\n");
	init_idt();

	kprintf("init_gdt\n");
	init_gdt();

	kprintf("init_page_table\n");
	init_page_table();

	kprintf("init_fpu\n");
	init_fpu();

	kprintf("init_lapic\n");
	init_lapic();

	kprintf("setup_x86 done.\n");
}

void handle_interrupt(int vector, struct x86_regs *regs)
{
	struct aal_mc_interrupt_handler *h;

	if (vector < 0 || vector > 255) {
		panic("Invalid interrupt vector.");
	} else if (vector < 32) {
		if (vector == 8 || 
		    (vector >= 10 && vector <= 15) || vector == 17) {
			kprintf("Exception %d at %lx:%lx\n",
			        vector, regs->rflags, regs->cs);
		} else {
			kprintf("Exception %d at %lx:%lx\n",
			        vector, regs->cs, regs->rip);
		}
		panic("Unhandled excepion");
	} else {
		list_for_each_entry(h, &handlers[vector - 32], list) {
			if (h->func) {
				h->func(h->priv);
			}
		}
	}

	lapic_ack();
}

void gpe_handler(struct x86_regs *regs)
{
	kprintf("General protection fault (err: %lx, %lx:%lx)\n",
	        regs->error, regs->cs, regs->rip);
	panic("GPF");
}

void x86_issue_ipi(int apicid, int vector)
{
	kprintf("issue interrupt:%d, %d\n", apicid, vector);
	lapic_write(LAPIC_ICR2, (unsigned int)apicid << 24);
	lapic_write(LAPIC_ICR0, vector);
}

/** AAL Functions **/

void cpu_halt(void)
{
	asm volatile("hlt");
}

void cpu_enable_interrupt(void)
{
	asm volatile("sti");
}

void cpu_disable_interrupt(void)
{
	asm volatile("cli");
}

void cpu_restore_interrupt(unsigned long flags)
{
	asm volatile("push %0; popf" : : "g"(flags) : "memory", "cc");
}

unsigned long cpu_disable_interrupt_save(void)
{
	unsigned long flags;

	asm volatile("pushf; pop %0; cli" : "=r"(flags) : : "memory", "cc");

	return flags;
}

int aal_mc_register_interrupt_handler(int vector,
                                      struct aal_mc_interrupt_handler *h)
{
	if (vector < 32 || vector > 255) {
		return -EINVAL;
	}

	list_add_tail(&h->list, &handlers[vector - 32]);

	return 0;
}
int aal_mc_unregister_interrupt_handler(int vector,
                                        struct aal_mc_interrupt_handler *h)
{
	list_del(&h->list);

	return 0;
}

extern unsigned long __page_fault_handler_address;

void aal_mc_set_page_fault_handler(void (*h)(unsigned long, void *))
{
	__page_fault_handler_address = (unsigned long)h;
}
