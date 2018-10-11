
#ifndef ARCH_PERF_EVENT_H
#define ARCH_PERF_EVENT_H

#include <linux/perf_event.h>

union perf_capabilities {
	struct {
		u64	lbr_format:6;
		u64	pebs_trap:1;
		u64	pebs_arch_reg:1;
		u64	pebs_format:4;
		u64	smm_freeze:1;
		/*
		 * PMU supports separate counter range for writing
		 * values > 32bit.
		 */
		u64	full_width_write:1;
	};
	u64	capabilities;
};

struct x86_pmu_quirk {
	struct x86_pmu_quirk *next;
	void (*func)(void);
};

struct extra_reg {
	unsigned int		event;
	unsigned int		msr;
	u64			config_mask;
	u64			valid_mask;
	int			idx;  /* per_xxx->regs[] reg index */
	bool			extra_msr_access;
};

#define MAX_LBR_ENTRIES		32

enum {
	X86_PERF_KFREE_SHARED = 0,
	X86_PERF_KFREE_EXCL   = 1,
	X86_PERF_KFREE_MAX
};

enum {
	x86_lbr_exclusive_lbr,
	x86_lbr_exclusive_bts,
	x86_lbr_exclusive_pt,
	x86_lbr_exclusive_max,
};

struct cpu_hw_events {
	/*
	 * Generic x86 PMC bits
	 */
	struct perf_event	*events[X86_PMC_IDX_MAX]; /* in counter order */
	unsigned long		active_mask[BITS_TO_LONGS(X86_PMC_IDX_MAX)];
	unsigned long		running[BITS_TO_LONGS(X86_PMC_IDX_MAX)];
	int			enabled;

	int			n_events; /* the # of events in the below arrays */
	int			n_added;  /* the # last events in the below arrays;
					     they've never been enabled yet */
	int			n_txn;    /* the # last events in the below arrays;
					     added in the current transaction */
	int			assign[X86_PMC_IDX_MAX]; /* event to counter assignment */
	u64			tags[X86_PMC_IDX_MAX];

	struct perf_event	*event_list[X86_PMC_IDX_MAX]; /* in enabled order */
	struct event_constraint	*event_constraint[X86_PMC_IDX_MAX];

	int			n_excl; /* the number of exclusive events */

	unsigned int		txn_flags;
	int			is_fake;

	/*
	 * Intel DebugStore bits
	 */
	struct debug_store	*ds;
	u64			pebs_enabled;
	int			n_pebs;
	int			n_large_pebs;

	/*
	 * Intel LBR bits
	 */
	int				lbr_users;
	struct perf_branch_stack	lbr_stack;
	struct perf_branch_entry	lbr_entries[MAX_LBR_ENTRIES];
	struct er_account		*lbr_sel;
	u64				br_sel;

	/*
	 * Intel host/guest exclude bits
	 */
	u64				intel_ctrl_guest_mask;
	u64				intel_ctrl_host_mask;
	struct perf_guest_switch_msr	guest_switch_msrs[X86_PMC_IDX_MAX];

	/*
	 * Intel checkpoint mask
	 */
	u64				intel_cp_status;

	/*
	 * manage shared (per-core, per-cpu) registers
	 * used on Intel NHM/WSM/SNB
	 */
	struct intel_shared_regs	*shared_regs;
	/*
	 * manage exclusive counter access between hyperthread
	 */
	struct event_constraint *constraint_list; /* in enable order */
	struct intel_excl_cntrs		*excl_cntrs;
	int excl_thread_id; /* 0 or 1 */

	/*
	 * AMD specific bits
	 */
	struct amd_nb			*amd_nb;
	/* Inverted mask of bits to clear in the perf_ctr ctrl registers */
	u64				perf_ctr_virt_mask;

	void				*kfree_on_online[X86_PERF_KFREE_MAX];
};

/*
 * struct x86_pmu - generic x86 pmu
 * TODO: add test with dwarf-extract-struct, comparing e.g.
 * kernel/arch/x86/events/intel/intel-rapl-perf.ko.debug
 * kmod/ihk-smp-x86_64.ko
 * x86_pmu extra_regs
 */
struct x86_pmu {
	/*
	 * Generic x86 PMC bits
	 */
	const char	*name;
	int		version;
	int		(*handle_irq)(struct pt_regs *);
	void		(*disable_all)(void);
	void		(*enable_all)(int added);
	void		(*enable)(struct perf_event *);
	void		(*disable)(struct perf_event *);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 0) || \
    (defined(RHEL_RELEASE_CODE) && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7, 4))
	void		(*add)(struct perf_event *);
	void		(*del)(struct perf_event *);
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)
	void		(*read)(struct perf_event *event);
#endif
	int		(*hw_config)(struct perf_event *event);
	int		(*schedule_events)(struct cpu_hw_events *cpuc, int n, int *assign);
	unsigned	eventsel;
	unsigned	perfctr;
	int		(*addr_offset)(int index, bool eventsel);
	int		(*rdpmc_index)(int index);
	u64		(*event_map)(int);
	int		max_events;
	int		num_counters;
	int		num_counters_fixed;
	int		cntval_bits;
	u64		cntval_mask;
	union {
			unsigned long events_maskl;
			unsigned long events_mask[BITS_TO_LONGS(ARCH_PERFMON_EVENTS_COUNT)];
	};
	int		events_mask_len;
	int		apic;
	u64		max_period;
	struct event_constraint *
			(*get_event_constraints)(struct cpu_hw_events *cpuc,
						 int idx,
						 struct perf_event *event);

	void		(*put_event_constraints)(struct cpu_hw_events *cpuc,
						 struct perf_event *event);

	void		(*start_scheduling)(struct cpu_hw_events *cpuc);

	void		(*commit_scheduling)(struct cpu_hw_events *cpuc, int idx, int cntr);

	void		(*stop_scheduling)(struct cpu_hw_events *cpuc);

	struct event_constraint *event_constraints;
	struct x86_pmu_quirk *quirks;
	int		perfctr_second_write;
	bool		late_ack;
	unsigned	(*limit_period)(struct perf_event *event, unsigned l);

	/*
	 * sysfs attrs
	 */
	int		attr_rdpmc_broken;
	int		attr_rdpmc;
	struct attribute **format_attrs;
	struct attribute **event_attrs;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)
	struct attribute **caps_attrs;
#endif

	ssize_t		(*events_sysfs_show)(char *page, u64 config);
	struct attribute **cpu_events;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 13, 0)
	unsigned long attr_freeze_on_smi;
	struct attribute *attrs;
#endif

	/*
	 * CPU Hotplug hooks
	 */
	int		(*cpu_prepare)(int cpu);
	void		(*cpu_starting)(int cpu);
	void		(*cpu_dying)(int cpu);
	void		(*cpu_dead)(int cpu);

	void		(*check_microcode)(void);
	void		(*sched_task)(struct perf_event_context *ctx,
				      bool sched_in);

	/*
	 * Intel Arch Perfmon v2+
	 */
	u64			intel_ctrl;
	union perf_capabilities intel_cap;

	/*
	 * Intel DebugStore bits
	 */
	unsigned int	bts		:1,
			bts_active	:1,
			pebs		:1,
			pebs_active	:1,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)
			pebs_broken	:1,
			pebs_prec_dist	:1,
			pebs_no_tlb     :1;
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(4, 5, 0) || \
	defined(RHEL_RELEASE_CODE) && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7, 3)
			pebs_broken	:1,
			pebs_prec_dist	:1;
#else
			pebs_broken	:1;
#endif
	int		pebs_record_size;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 0) || \
	defined(RHEL_RELEASE_CODE) && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7, 3)
	int		pebs_buffer_size;
#endif
	void		(*drain_pebs)(struct pt_regs *regs);
	struct event_constraint *pebs_constraints;
	void		(*pebs_aliases)(struct perf_event *event);
	int 		max_pebs_events;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 16, 0)
	unsigned long	large_pebs_flags;
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(4, 3, 0) || \
	defined(RHEL_RELEASE_CODE) && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7, 3)
	unsigned long	free_running_flags;
#endif

	/*
	 * Intel LBR
	 */
	unsigned long	lbr_tos, lbr_from, lbr_to; /* MSR base regs       */
	int		lbr_nr;			   /* hardware stack size */
	u64		lbr_sel_mask;		   /* LBR_SELECT valid bits */
	const int	*lbr_sel_map;		   /* lbr_select mappings */
	bool		lbr_double_abort;	   /* duplicated lbr aborts */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 7, 0) || \
	defined(RHEL_RELEASE_CODE) && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7, 4)
	bool		lbr_pt_coexist;		   /* (LBR|BTS) may coexist with PT */
#endif

	/*
	 * Intel PT/LBR/BTS are exclusive
	 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0) || \
	defined(RHEL_RELEASE_CODE) && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7, 3)
	atomic_t	lbr_exclusive[x86_lbr_exclusive_max];
#endif

	/*
	 * AMD bits
	 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 0) || \
	defined(RHEL_RELEASE_CODE) && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7, 3)
	unsigned int	amd_nb_constraints : 1;
#endif

	/*
	 * Extra registers for events
	 */
	struct extra_reg *extra_regs;
	unsigned int flags;

	/*
	 * Intel host/guest support (KVM)
	 */
	struct perf_guest_switch_msr *(*guest_get_msrs)(int *nr);
};

#endif
