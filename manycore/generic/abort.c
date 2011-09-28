#include <aal/debug.h>
#include <aal/cpu.h>

void panic(const char *msg)
{
	cpu_disable_interrupt();

	kprintf(msg);

	while (1) {
		cpu_halt();
	}
}

