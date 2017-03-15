/* smp-arm64-trampoline_64.S COPYRIGHT FUJITSU LIMITED 2015 */
/*
 * Kloader transition code:
 */
#include <linux/threads.h>
#include "../../../../../cokernel/smp/arm64/bootparam.h"

.section .rodata, "a", @progbits

.globl ihk_smp_trampoline_data
base = .
ihk_smp_trampoline_data:
	b	cpu_start_body

	.org	8
header_pgtbl:
	.quad	0		/* page table address */
header_load:	
	.quad	0		/* load address */
stack_ptr:
	.quad	0		/* initial stack */
notify_address:
	.quad	0		/* where to notify? */
startup_data:
	.quad	0		/* startup_data addr */
st_phys_base:
	.quad	0		/* straight map base address */
st_phys_size:
	.quad	0		/* straight map size */
dist_base_pa:
	.quad	0		/* GIC distributor register base addr */
dist_map_size:
	.quad	0		/* GIC distributor register map size */
cpu_base_pa:
	.quad	0		/* GIC cpu interface register base addr */
cpu_map_size:
	.quad	0		/* GIC cpu interface register map size */
percpu_offset:
	.word	0		/* GIC cpu interface register map offset value */
gic_version:
	.word	0		/* GIC version value */
loops_per_jiffy:
	.quad	0		/* udelay loops value */
hz:
	.quad	0		/* HZ value */
psci_method:
	.quad	0		/* hvc or smc ? */
use_virt_timer:
	.quad	0		/* virt timer or phys timer ? */
evtstrm_timer_rate:
	.quad	0		/* event stream timer rate */
cpu_logical_map_size:
	.quad	0		/* the cpu-core maximun number */
cpu_logical_map:
	.skip	NR_CPUS * 8	/* array of the MPIDR and the core number */
rdist_base_pa:
	.skip	NR_CPUS * 8	/* GIC re-distributor register base addresses */
nr_pmu_irq_affiniry:
	.word	0		/* number of pmu affinity list elements. */
pmu_irq_affiniry:
	.skip	SMP_MAX_CPUS * 4	/* array of the pmu affinity list */
cpu_start_body:
	adr	x4, ihk_smp_trampoline_data	/* get ihk_smp_trampoline_data PA into x4 */
	adr	x5, startup_data		/* get startup_data PA */
	ldr	x27, header_load
	br	x27	/* jump to startup. */
	b	.
.globl ihk_smp_trampoline_end
ihk_smp_trampoline_end:
