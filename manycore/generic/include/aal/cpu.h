#ifndef AAL_CPU_H
#define AAL_CPU_H

#include <list.h>

void cpu_enable_interrupt(void);
void cpu_disable_interrupt(void);
void cpu_halt(void);
void cpu_restore_interrupt(unsigned long);
unsigned long cpu_disable_interrupt_save(void);

struct aal_mc_interrupt_handler {
	struct list_head list;
	void (*func)(void *);
	void *priv;
};

#endif

