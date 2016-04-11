#ifndef __ARCH_DEFS_H
#define __ARCH_DEFS_H

/** \brief Default IKC queue mapping attribute */
#define IHK_IKC_QUEUE_PT_ATTR IHK_MAP_FLAG_NOCACHE

struct ihk_cache_topology {
	struct list_head chain;
	int index;
	int padding;
	long level;
	char *type;
	long size;
	char *size_str;
	long coherency_line_size;
	long number_of_sets;
	long physical_line_partition;
	long ways_of_associativity;
	cpumask_t shared_cpu_map;
};

struct ihk_cpu_topology {
	struct list_head chain;
	int cpu_number;
	int hw_id;
	long physical_package_id;
	long core_id;
	cpumask_t core_siblings;
	cpumask_t thread_siblings;
	struct list_head cache_topology_list;
};

struct ihk_node_topology {
	struct list_head chain;
	int node_number;
	int padding;
	cpumask_t cpumap;
};

#endif
