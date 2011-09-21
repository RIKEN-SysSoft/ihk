#include <aal/cpu.h>

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
