#ifndef __INPUT_VECTOR_H_INCLUDED__
#define __INPUT_VECTOR_H_INCLUDED__

#define MAX_NUM_CPUS 272

struct cpus {
	int *cpus;
	int ncpus;
};

struct ikc_cpu_map {
	struct ihk_ikc_cpu_map *map;
	int ncpus;
};

int cpus_init(struct cpus *cpus, int ncpus);
int cpus_copy(struct cpus *dst, struct cpus *src);
int _cpus_ls(struct cpus *cpus, char *online, int nlinux, int nmck);
int cpus_ls(struct cpus *cpus);
int cpus_ls_online(struct cpus *cpus, int nlinux, int nmck);
int cpus_max_id(struct cpus *cpus);
int cpus_push(struct cpus *cpus, int id);
int cpus_pop(struct cpus *cpus, int n);
int cpus_shift(struct cpus *cpus, int n);
void cpus_fill(struct cpus *cpus, int id);
void cpus_dump(struct cpus *cpus);
int cpus_compare(struct cpus *cpus_result, struct cpus *cpus_expected);
int cpus_check_reserved(struct cpus *expected);

int cpus_ncpus_offline(void);
int _cpus_reserve(int nlinux, int nmck);
int cpus_reserve(void);
int cpus_release(void);

int cpus_reserved(struct cpus *cpus);
int cpus_check_assigned(struct cpus *expected);

int cpus_os_assign(void);
int cpus_os_release(void);

int cpus_at(struct cpus *cpus, int index);
int cpus_broadcast(struct cpus *cpus, int ncpus);

int cpus_toggle(int cpu_id, char *toggle);


void ikc_cpu_map_dump(struct ikc_cpu_map *map);
int ikc_cpu_map_init(struct ikc_cpu_map *map, int ncpus);
int ikc_cpu_map_copy(struct ikc_cpu_map *map, struct cpus *src_cpus,
		     struct cpus *dst_cpus);
int ikc_cpu_map_cat(struct ikc_cpu_map *a, struct ikc_cpu_map *b,
		    struct ikc_cpu_map *c);
int ikc_cpu_map_compare(struct ikc_cpu_map *result,
			struct ikc_cpu_map *expected);
int ikc_cpu_map_check(struct ikc_cpu_map *expected);
int ikc_cpu_map_2toN(struct ikc_cpu_map *map);
void ikc_cpu_map_max_src_cpu(struct ikc_cpu_map *map, int *src_cpu,
			     int *dst_cpu);
int ikc_cpu_map_push(struct ikc_cpu_map *map, int src_cpu, int dst_cpu);
int ikc_cpu_map_pop(struct ikc_cpu_map *map, int n);

int ikc_cpu_map_check_channels(int nchannels);

#endif
