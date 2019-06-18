#ifndef __LIB_RUSAGE_H
#define __LIB_RUSAGE_H

#define IHK_MAX_NUM_NUMA_NODES 1024
#define IHK_MAX_NUM_CPUS 1024

enum ihk_os_pgsize {
	IHK_OS_PGSIZE_4KB,
	IHK_OS_PGSIZE_64KB,
	IHK_OS_PGSIZE_2MB,
	IHK_OS_PGSIZE_32MB,
	IHK_OS_PGSIZE_1GB,
	IHK_OS_PGSIZE_16GB,
	IHK_OS_PGSIZE_512MB,
	IHK_OS_PGSIZE_4TB,
	IHK_MAX_NUM_PGSIZES
};

static inline long rusage_pgtype_to_pgsize(enum ihk_os_pgsize pgtype)
{
	long ret;

	switch (pgtype) {
	case IHK_OS_PGSIZE_4KB:
		ret = 1UL << 12;
		break;
	case IHK_OS_PGSIZE_64KB:
		ret = 1UL << 16;
		break;
	case IHK_OS_PGSIZE_2MB:
		ret = 1UL << 21;
		break;
	case IHK_OS_PGSIZE_32MB:
		ret = 1UL << 25;
		break;
	case IHK_OS_PGSIZE_1GB:
		ret = 1UL << 30;
		break;
	case IHK_OS_PGSIZE_16GB:
		ret = 1UL << 34;
		break;
	case IHK_OS_PGSIZE_512MB:
		ret = 1UL << 29;
		break;
	case IHK_OS_PGSIZE_4TB:
		ret = 1UL << 42;
		break;
	default:
		ret = -1;
	}

	return ret;
}

struct ihk_os_rusage {
	unsigned long memory_stat_rss[IHK_MAX_NUM_PGSIZES];
	unsigned long memory_stat_mapped_file[IHK_MAX_NUM_PGSIZES];
	unsigned long memory_max_usage;
	unsigned long memory_kmem_usage;
	unsigned long memory_kmem_max_usage;
	unsigned long memory_numa_stat[IHK_MAX_NUM_NUMA_NODES];
	unsigned long cpuacct_stat_system;
	unsigned long cpuacct_stat_user;
	unsigned long cpuacct_usage;
	unsigned long cpuacct_usage_percpu[IHK_MAX_NUM_CPUS];
	int num_threads;
	int max_num_threads;
};

#endif
