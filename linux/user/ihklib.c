/**
 * \file ihklib.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <bfd.h>
#include <inttypes.h>
#include <time.h>
#include <limits.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/eventfd.h>
#include <sys/time.h>

#include <config.h>
#include <ihk/ihk_host_user.h>
#include <ihk/ihklib.h>
#include <ihk/ihklib_private.h>
#include <ihk/ihk_arch_rusage.h>

int __argc;
char **__argv;

//#define DEBUG

#ifdef DEBUG
#define	dprintf(...)											\
	do {														\
		char msg[1024];											\
		sprintf(msg, __VA_ARGS__);								\
		fprintf(stderr, "%s,%s", __FUNCTION__, msg);			\
	} while (0);
#define	eprintf(...)											\
	do {														\
		char msg[1024];											\
		sprintf(msg, __VA_ARGS__);								\
		fprintf(stderr, "%s,%s", __FUNCTION__, msg);			\
	} while (0);
#else
#define dprintf(...) do {  } while (0)
#define eprintf(...) do {  } while (0)
#endif

#define PHYSMEM_NAME_SIZE 32

#define CHKANDJUMP(cond, err, ...)										\
	do {																\
		if(cond) {														\
			eprintf(__VA_ARGS__);										\
			ret = err;													\
			goto out;												\
		}																\
	} while(0)

static void cpus_array2str(char* cpu_list, ssize_t sz_cpu_list, int num_cpus, int* cpus) {
	int i;
	char cpu_str[64];
	memset(cpu_list, 0, sz_cpu_list);
	for (i = 0; i < num_cpus; i++) {
		snprintf(cpu_str, sizeof(cpu_str) - strlen(cpu_str) - 1, "%d", cpus[i]);
		strncat(cpu_list, cpu_str, sz_cpu_list - strlen(cpu_list) - 1);
		if(i != num_cpus - 1) {
			strncat(cpu_list, ",", sz_cpu_list - strlen(cpu_list) - 1);
		}
	}
}

static int cpus_str2array(char* cpu_list, int *num_cpus, int* cpus) {
	int cpu_rank = 0, ret = 0;
	char* token;
	
	CHKANDJUMP(strchr(cpu_list, '-'), -1, "range expression not supported,cpu_list=%s\n", cpu_list);
	
	token = strsep(&cpu_list, ",");
	while (token != NULL) {
		if(*token == 0) {
			goto empty_cpu;
		}
		if(*num_cpus > cpu_rank) {
			cpus[cpu_rank] = atol(token);
			dprintf("%s,cpus[%d]=%d\n", __FUNCTION__, cpu_rank, cpus[cpu_rank]);
		}	
		cpu_rank++;
	empty_cpu:
		token = strsep(&cpu_list, ",");
	}
	*num_cpus = cpu_rank;
	dprintf("%s,num_cpus=%d\n", __FUNCTION__, *num_cpus);

 out:
	return ret;
}

static void mem_array2str(char* mem_list, ssize_t sz_mem_list, int num_mem_chunks, struct ihk_mem_chunk* mem_chunks) {
	int i;
	char mem_str[128];

	memset(mem_list, 0, sz_mem_list);
	for (i = 0; i < num_mem_chunks; i++) {
		snprintf(mem_str, sizeof(mem_str) - strlen(mem_str) - 1, "%lu@%d",
				 mem_chunks[i].size, mem_chunks[i].numa_node_number);
		strncat(mem_list, mem_str, sz_mem_list - strlen(mem_list) - 1);
		if(i != num_mem_chunks - 1) {
			strncat(mem_list, ",", sz_mem_list - strlen(mem_list) - 1);
		}
	}
	dprintf("%s,mem_list=%s\n", __FUNCTION__, mem_list);
}

static int mem_str2array(char* mem_list, int *num_mem_chunks, struct ihk_mem_chunk* mem_chunks) {
	int ret = 0;
	int mem_count = 0;
	char* chunk = mem_list;
	char* token = strsep(&chunk, ",");
	while (token != NULL) {
		if(*token == 0) {
			goto empty_mem;
		}
		char* cdr = token;
		token = strsep(&cdr, "@");
		if(*num_mem_chunks > mem_count) {
			mem_chunks[mem_count].size = atol(token);
			dprintf("%s,size[%d]=%ld\n", __FUNCTION__, mem_count, mem_chunks[mem_count].size);
			if(cdr != NULL) {
				mem_chunks[mem_count].numa_node_number = atol(cdr);
				dprintf("%s,numa_node_number[%d]=%d\n", __FUNCTION__, mem_count, mem_chunks[mem_count].numa_node_number);
			}
		}
		mem_count++;
	empty_mem:
		token = strsep(&chunk, ",");
	}
	*num_mem_chunks = mem_count;
	//out:
    return ret;
}

int ihklib_device_open(int index) 
{
	int ret = 0, ret_glibc;
	char fn[1024];
	int fd = -1;
	struct stat file_stat;

	sprintf(fn, "/dev/mcd%d", index);
	ret_glibc = stat(fn, &file_stat);
	CHKANDJUMP(ret_glibc != 0, -ENOENT, "IHK device (/dev/mcd%d) not found\n", index);
 
	fd = open(fn, O_RDONLY);

	ret = fd;
out:
	return ret;
}

int ihk_reserve_cpu(int index, int* cpus, int num_cpus)
{
	int ret = 0, ret_ioctl;
	char cpu_list[IHK_MAX_NUM_CPUS];
	struct ihk_ioctl_desc req;
	int fd = -1;
	
	CHKANDJUMP(num_cpus > IHK_MAX_NUM_CPUS, -EINVAL, "too many cpus requested\n");

	fd = ihklib_device_open(index);
	CHKANDJUMP(fd < 0, -errno, "ihklib_device_open failed\n");

	cpus_array2str(cpu_list, sizeof(cpu_list), num_cpus, cpus);

	req.string = cpu_list;
	req.string_len = strlen(cpu_list);
	CHKANDJUMP(!req.string || !req.string_len, -EINVAL, "invalid format, string=%s\n", cpu_list);

	ret_ioctl = ioctl(fd, IHK_DEVICE_RESERVE_CPU, &req);
	CHKANDJUMP(ret_ioctl != 0, -errno, "ioctl failed, string=%s\n", cpu_list);
	
 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}

int ihk_get_num_reserved_cpus(int index)
{
	int ret = 0, ret_ioctl, ret_ihklib;
    char result[8 * IHK_MAX_NUM_CPUS];
	int num_cpus = 0;
	int fd = -1;

	fd = ihklib_device_open(index);
    CHKANDJUMP(fd < 0, -errno, "ihklib_device_open failed\n");

	memset(result, 0, sizeof(result));

	ret_ioctl = ioctl(fd, IHK_DEVICE_QUERY_CPU, result);
	CHKANDJUMP(ret_ioctl != 0, -errno, "ioctl failed\n");
	dprintf("%s,result=%s\n", __FUNCTION__, result);

	ret_ihklib = cpus_str2array(result, &num_cpus, NULL);
	CHKANDJUMP(ret_ihklib != 0, -EINVAL, "cpus_str2array failed\n");
	dprintf("%s,def,num_cpus=%d\n", __FUNCTION__, num_cpus);

	ret = num_cpus;

 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}

int ihk_query_cpu(int index, int* cpus, int _num_cpus)
{
	int ret = 0, ret_ioctl, ret_ihklib; 
    char result[8 * IHK_MAX_NUM_CPUS];
	int num_cpus = _num_cpus;
	int fd = -1;

    CHKANDJUMP(num_cpus > IHK_MAX_NUM_CPUS, -EINVAL, "too many cpus requested\n");

	fd = ihklib_device_open(index);
    CHKANDJUMP(fd < 0, -errno, "ihklib_device_open failed\n");

	memset(result, 0, sizeof(result));

	ret_ioctl = ioctl(fd, IHK_DEVICE_QUERY_CPU, result);
	CHKANDJUMP(ret_ioctl != 0, -errno, "ioctl failed\n");
	dprintf("%s,result=%s\n", __FUNCTION__, result);

	ret_ihklib = cpus_str2array(result, &num_cpus, cpus);
	CHKANDJUMP(ret_ihklib != 0, -EINVAL, "cpus_str2array failed\n");
	dprintf("%s,def,num_cpus=%d\n", __FUNCTION__, num_cpus);

	CHKANDJUMP(num_cpus != _num_cpus, -EINVAL, "actual number of CPUs (%d) is different than requested (%d)\n", num_cpus, _num_cpus);

 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}

int ihk_release_cpu(int index, int* cpus, int num_cpus)
{
	int ret = 0, ret_ioctl;
	struct ihk_ioctl_desc req;
	char cpu_list[IHK_MAX_NUM_CPUS];
	int fd = -1;
	
    CHKANDJUMP(num_cpus > IHK_MAX_NUM_CPUS, -EINVAL, "too many cpus specified\n");

	fd = ihklib_device_open(index);
	CHKANDJUMP(fd < 0, -errno, "ihklib_device_open failed\n");

	cpus_array2str(cpu_list, sizeof(cpu_list), num_cpus, cpus);

	req.string = cpu_list;
	req.string_len = strlen(cpu_list);
	CHKANDJUMP(!req.string || !req.string_len, -EINVAL, "invalid format, string=%s\n", cpu_list);

	ret_ioctl = ioctl(fd, IHK_DEVICE_RELEASE_CPU, &req);
	CHKANDJUMP(ret_ioctl != 0, -errno, "ioctl failed, string=%s\n", cpu_list);
	
 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}

int ihk_reserve_mem(int index, struct ihk_mem_chunk* mem_chunks, int num_mem_chunks)
{
	int ret = 0, ret_ioctl;
	struct ihk_ioctl_desc req;
	char mem_list[16 * IHK_MAX_NUM_MEM_CHUNKS];
	int fd = -1;
	
	CHKANDJUMP(num_mem_chunks > IHK_MAX_NUM_MEM_CHUNKS, -EINVAL, "too many memory chunks requested\n");

	fd = ihklib_device_open(index);
	CHKANDJUMP(fd < 0, -errno, "ihklib_device_open failed\n");

	mem_array2str(mem_list, sizeof(mem_list), num_mem_chunks, mem_chunks);

	req.string = mem_list;
	req.string_len = strlen(mem_list);
	CHKANDJUMP(!req.string || !req.string_len, -EINVAL, "invalid format, list=%s\n", mem_list);

	ret_ioctl = ioctl(fd, IHK_DEVICE_RESERVE_MEM, &req);
	CHKANDJUMP(ret_ioctl != 0, -errno, "ioctl failed, list=%s\n", mem_list);

 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}

int ihk_get_num_reserved_mem_chunks(int index)
{
	int ret = 0, ret_ioctl, ret_ihklib; 
    char result[16 * IHK_MAX_NUM_MEM_CHUNKS];
	int num_mem_chunks = 0;
	int fd = -1;

    fd = ihklib_device_open(index);
    CHKANDJUMP(fd < 0, -errno, "ihklib_device_open failed\n");

	memset(result, 0, sizeof(result));

	ret_ioctl = ioctl(fd, IHK_DEVICE_QUERY_MEM, result);
    CHKANDJUMP(ret_ioctl != 0, -errno, "ioctl failed\n");
	dprintf("%s,ioctl returns %s\n", __FUNCTION__, result);

	ret_ihklib = mem_str2array(result, &num_mem_chunks, NULL);
	CHKANDJUMP(ret_ihklib != 0, -EINVAL, "mem_str2array failed\n");
	dprintf("%s,def,num_mem_chunks=%d\n", __FUNCTION__, num_mem_chunks);

	ret = num_mem_chunks;

 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}

int ihk_query_mem(int index, struct ihk_mem_chunk* mem_chunks, int _num_mem_chunks)
{
	int ret = 0, ret_ioctl, ret_ihklib;
    char result[16 * IHK_MAX_NUM_MEM_CHUNKS];
	int num_mem_chunks = _num_mem_chunks;
	int fd = -1;

    fd = ihklib_device_open(index);
    CHKANDJUMP(fd < 0, -errno, "ihklib_device_open failed\n");

	memset(result, 0, sizeof(result));

	ret_ioctl = ioctl(fd, IHK_DEVICE_QUERY_MEM, result);
    CHKANDJUMP(ret_ioctl != 0, -errno, "ioctl failed\n");
	dprintf("%s,ioctl returns %s\n", __FUNCTION__, result);

	ret_ihklib = mem_str2array(result, &num_mem_chunks, mem_chunks);
	CHKANDJUMP(ret_ihklib != 0, -EINVAL, "mem_str2array failed\n");
	dprintf("%s,def,num_mem_chunks=%d\n", __FUNCTION__, num_mem_chunks);

	CHKANDJUMP(num_mem_chunks != _num_mem_chunks, -EINVAL, "actual number of memory chunks (%d) is different than requested (%d)\n", num_mem_chunks, _num_mem_chunks);

 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}

int ihk_release_mem(int index, struct ihk_mem_chunk* mem_chunks, int num_mem_chunks)
{
	int ret = 0, ret_ioctl;
	struct ihk_ioctl_desc req;
	char mem_list[IHK_MAX_NUM_MEM_CHUNKS];
	int fd = -1;
	
    CHKANDJUMP(num_mem_chunks > IHK_MAX_NUM_MEM_CHUNKS, -EINVAL, "too many memory chunks specified\n");

	fd = ihklib_device_open(index);
    CHKANDJUMP(fd < 0, -errno, "ihklib_device_open failed\n");

	mem_array2str(mem_list, sizeof(mem_list), num_mem_chunks, mem_chunks);
	
	req.string = mem_list;
	req.string_len = strlen(mem_list);
	CHKANDJUMP(!req.string || !req.string_len, -EINVAL, "invalid format, string=%s\n", mem_list);

	ret_ioctl = ioctl(fd, IHK_DEVICE_RELEASE_MEM, &req);
	CHKANDJUMP(ret_ioctl != 0, -errno, "ioctl failed, string=%s\n", mem_list);
	
 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}

/* Create OS and return OS index */
int ihk_create_os(int index)
{
	int ret = 0, ret_ioctl; 
	int fd = -1;
	
    fd = ihklib_device_open(index);
    CHKANDJUMP(fd < 0, -errno, "ihklib_device_open failed\n");
	
	ret_ioctl = ioctl(fd, IHK_DEVICE_CREATE_OS, 0);
	CHKANDJUMP(ret_ioctl < 0, -errno, "ioctl failed\n");

	ret = ret_ioctl;
 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}

int ihk_get_num_os_instances(int index)
{
	int ret = 0;
	DIR *dir = NULL;
	struct dirent *direp;
	int num_os_instances = 0;
    int fd = -1;

    fd = ihklib_device_open(index);
    CHKANDJUMP(fd < 0, -errno, "ihklib_device_open failed\n");
	
	dir = opendir(PATH_DEV);
	CHKANDJUMP(dir == NULL, -EINVAL, "opendir failed\n");

	direp = readdir(dir);
	while (direp) {
		if ((strncmp(direp->d_name,"mcos",4) == 0)) {
			dprintf("dir:%s\n",direp->d_name);
			num_os_instances++;
			dprintf("count:%d \n",num_os_instances);
		}
		direp = readdir(dir);
	}
	ret = num_os_instances;
 out:
	if (fd != -1) {
		close(fd);
	}
	if (dir) {
		closedir(dir);
	}
	return ret;
}

int ihk_get_os_instances(int index, int *indices, int _num_os_instances)
{
	int ret = 0;
	DIR *dir = NULL;
	struct dirent *direp;
	int num_os_instances = 0;
	
	dir = opendir(PATH_DEV);
	CHKANDJUMP(dir == NULL, -EINVAL, "opendir failed\n");

	direp = readdir(dir);
	while (direp) {
		if ((strncmp(direp->d_name, "mcos", 4) == 0)) {
			dprintf("dir:%s\n", direp->d_name);
			indices[num_os_instances] = atoi(direp->d_name + 4);
			dprintf("indices[%d]=%d\n", num_os_instances, indices[num_os_instances]);
			num_os_instances++;
			if (num_os_instances > _num_os_instances) {
				ret = -EINVAL;
				eprintf("%s: Actual # of OS instances (%d) is different than requested (%d)\n", __FUNCTION__, num_os_instances, _num_os_instances);
				goto out;
			}
		}
		direp = readdir(dir);
	}
	CHKANDJUMP(num_os_instances != _num_os_instances, -EINVAL, "Actual # of OS instances (%d) is different than requested (%d)\n", num_os_instances, _num_os_instances);
	
 out:
	if (dir) {
		closedir(dir);
	}
	return ret;
}

int ihk_destroy_os(int dev_index, int os_index)
{
	int ret = 0, ret_ioctl;
	int fd = -1;
	
    fd = ihklib_device_open(dev_index);
    CHKANDJUMP(fd < 0, -errno, "ihklib_device_open failed\n");

	ret_ioctl = ioctl(fd, IHK_DEVICE_DESTROY_OS, os_index);
	CHKANDJUMP(ret_ioctl != 0, -errno, "ioctl failed\n");

 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}

int ihklib_os_open(int index)
{
	int ret = 0, ret_glibc;
	char fn[1024];
	int fd = -1;
	struct stat file_stat;

	sprintf(fn, "/dev/mcos%d", index);
	ret_glibc = stat(fn, &file_stat);
	CHKANDJUMP(ret_glibc != 0, -ENOENT, "os instance (/dev/mcos%d) not found\n", index);

	fd = open(fn, O_RDONLY);
	
	ret = fd;
 out:
	return ret;
}

int ihk_os_assign_cpu(int index, int* cpus, int num_cpus)
{
	int ret = 0, ret_ioctl;
	char cpu_list[IHK_MAX_NUM_CPUS];
	struct ihk_ioctl_desc req;
	int fd = -1;

    CHKANDJUMP(num_cpus > IHK_MAX_NUM_CPUS, -EINVAL, "too many cpus requested\n");

	fd = ihklib_os_open(index);
	CHKANDJUMP(fd < 0, -errno, "ihklib_os_open failed\n");

	cpus_array2str(cpu_list, sizeof(cpu_list), num_cpus, cpus);

	req.string = cpu_list;
	req.string_len = strlen(cpu_list);
	CHKANDJUMP(!req.string || !req.string_len, -EINVAL, "invalid format, string=%s\n", cpu_list);

	ret_ioctl = ioctl(fd, IHK_OS_ASSIGN_CPU, &req);
	CHKANDJUMP(ret_ioctl != 0, -errno, "ioctl failed, string=%s\n", cpu_list);
	
 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}

int ihk_os_get_num_assigned_cpus(int index)
{
	int ret = 0, ret_ioctl, ret_ihklib;
    char result[8 * IHK_MAX_NUM_CPUS];
	int cpus[IHK_MAX_NUM_CPUS];
	int num_cpus = 0;
	int fd = -1;

	fd = ihklib_os_open(index);
    CHKANDJUMP(fd < 0, -errno, "ihklib_os_open failed\n");
	
	memset(result, 0, sizeof(result));

	ret_ioctl = ioctl(fd, IHK_OS_QUERY_CPU, result);
	CHKANDJUMP(ret_ioctl != 0, -errno, "ioctl failed\n");
	dprintf("%s,result=%s\n", __FUNCTION__, result);

	ret_ihklib = cpus_str2array(result, &num_cpus, cpus);
	CHKANDJUMP(ret_ihklib != 0, -EINVAL, "cpus_str2array failed\n");
	dprintf("%s,def,num_cpus=%d\n", __FUNCTION__, num_cpus);

	ret = num_cpus;

 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}

int ihk_os_query_cpu(int index, int* cpus, int _num_cpus)
{
	int ret = 0, ret_ioctl, ret_ihklib;
    char result[8 * IHK_MAX_NUM_CPUS];
	int num_cpus = _num_cpus;
	int fd = -1;

    CHKANDJUMP(num_cpus > IHK_MAX_NUM_CPUS, -EINVAL, "too many cpus requested\n");

	fd = ihklib_os_open(index);
	CHKANDJUMP(fd < 0, -errno, "ihklib_os_open failed\n");
	
	memset(result, 0, sizeof(result));

	ret_ioctl = ioctl(fd, IHK_OS_QUERY_CPU, result);
	CHKANDJUMP(ret_ioctl != 0, -errno, "ioctl failed\n");
	dprintf("%s,result=%s\n", __FUNCTION__, result);

	ret_ihklib = cpus_str2array(result, &num_cpus, cpus);
	CHKANDJUMP(ret_ihklib != 0, -EINVAL, "cpus_str2array failed\n");
	dprintf("%s,def,num_cpus=%d\n", __FUNCTION__, num_cpus);

	CHKANDJUMP(num_cpus != _num_cpus, -EINVAL, "actual number of CPUs (%d) is different than requested (%d)\n", num_cpus, _num_cpus);

 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}

int ihk_os_release_cpu(int index, int* cpus, int num_cpus)
{
	int ret = 0, ret_ioctl;
	struct ihk_ioctl_desc req;
	char cpu_list[IHK_MAX_NUM_CPUS];
	int fd = -1;
	
    CHKANDJUMP(num_cpus > IHK_MAX_NUM_CPUS, -EINVAL, "too many cpus specified\n");

	fd = ihklib_os_open(index);
	CHKANDJUMP(fd < 0, -errno, "ihklib_os_open failed\n");

	cpus_array2str(cpu_list, sizeof(cpu_list), num_cpus, cpus);

	req.string = cpu_list;
	req.string_len = strlen(cpu_list);
	CHKANDJUMP(!req.string || !req.string_len, -EINVAL, "invalid format, string=%s\n", cpu_list);

	dprintf("string=%s\n", cpu_list);
	ret_ioctl = ioctl(fd, IHK_OS_RELEASE_CPU, &req);
	CHKANDJUMP(ret_ioctl != 0, -errno, "ioctl failed, string=%s\n", cpu_list);
	
 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}

int ihk_os_set_ikc_map(int index, struct ihk_ikc_cpu_map *map, int num_cpus)
{
	int ret = 0, ret_ioctl;
	int i, j;
	char map_str[8 * (IHK_MAX_NUM_CPUS + IHK_MAX_NUM_NUMA_NODES)];
    char cpu_str[16];
	int fd = -1;

	int dst_existence[IHK_MAX_NUM_CPUS];
	int num_ssets = 0;
	int dst_sorted[IHK_MAX_NUM_CPUS];

    CHKANDJUMP(num_cpus > IHK_MAX_NUM_CPUS, -EINVAL, "too many cpus specified\n");

	fd = ihklib_os_open(index);
	CHKANDJUMP(fd < 0, -errno, "ihklib_os_open failed\n");

	memset(dst_existence, 0, sizeof(dst_existence));
	memset(dst_sorted, 0, sizeof(dst_sorted));

	for (i = 0; i < num_cpus; i++) {
		if (dst_existence[map[i].dst_cpu] == 0) {
			dst_existence[map[i].dst_cpu] = 1;
			dst_sorted[num_ssets] = map[i].dst_cpu;
			num_ssets++;
		}
	}

	memset(map_str, 0, sizeof(map_str));
	for (i = 0; i < num_ssets; i++) {
		if(i != 0) {
			strcat(map_str, "+");
		}
		int sset_size = 0;
		for (j = 0; j < num_cpus; j++) {
			if (map[j].dst_cpu == dst_sorted[i]) {
				if(sset_size != 0) {
					strcat(map_str, ",");
				}
				snprintf(cpu_str, sizeof(cpu_str), "%d", map[j].src_cpu);
				strcat(map_str, cpu_str);
				sset_size++;
			}
		}
		strcat(map_str, ":");
		snprintf(cpu_str, sizeof(cpu_str), "%d", dst_sorted[i]);
		strcat(map_str, cpu_str);
	}
	dprintf("%s,map_str=%s\n", __FUNCTION__, map_str);

	ret_ioctl = ioctl(fd, IHK_OS_SET_IKC_MAP, map_str);
    CHKANDJUMP(ret_ioctl != 0, -errno, "ioctl failed\n");

 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}

int ihk_os_get_ikc_map(int index, struct ihk_ikc_cpu_map *map, int num_cpus)
{
	int ret = 0, ret_ioctl;
    char query_result[8 * (IHK_MAX_NUM_CPUS + IHK_MAX_NUM_NUMA_NODES)];
	int fd = -1;

    CHKANDJUMP(num_cpus > IHK_MAX_NUM_CPUS, -EINVAL, "too many cpus specified\n");

	fd = ihklib_os_open(index);
	CHKANDJUMP(fd < 0, -errno, "ihklib_os_open failed\n");

	dprintf("%s,get_ikc_map\n", __FUNCTION__);
	memset(query_result, 0, sizeof(query_result));

	ret_ioctl = ioctl(fd, IHK_OS_GET_IKC_MAP, query_result);
    CHKANDJUMP(ret_ioctl != 0, -errno, "ioctl failed\n");
	
	dprintf("%s,get_ikc_map, ioctl returned %s\n", __FUNCTION__, query_result);

	char *sset = query_result;
	char *token = strsep(&sset, "+");
	int pair_rank = 0;
	while (token != NULL) {	
		if(*token == 0) {
			goto empty_pair;
		}
		char *cdr = token;
		token = strsep(&cdr, ":");
		CHKANDJUMP(*token == 0, -EINVAL, "get_ikc_map,empty LWK cpus,str=%s\n", query_result);
		CHKANDJUMP(cdr == NULL, -EINVAL, "get_ikc_map,colon not found,str=%s\n", query_result);

		char* cpus = token;
		token = strsep(&cpus, ",");
		while (token != NULL) {
			if(*token == 0) {
				goto empty_cpu;
			}
			map[pair_rank].src_cpu = atol(token);
			dprintf("map[%d].src_cpu=%d\n", pair_rank, map[pair_rank].src_cpu);
			map[pair_rank].dst_cpu = atol(cdr);
			dprintf("map[%d].dst_cpu=%d\n", pair_rank, map[pair_rank].dst_cpu);
			pair_rank++;
		empty_cpu:
			token = strsep(&cpus, ",");
		}
	empty_pair:
		token = strsep(&sset, "+");
	}

 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}

int ihk_os_assign_mem(int index, struct ihk_mem_chunk *mem_chunks, int num_mem_chunks)
{
	int ret = 0, ret_ioctl;
	struct ihk_ioctl_desc req;
    char mem_list[16 * IHK_MAX_NUM_MEM_CHUNKS];
	int fd = -1;

	CHKANDJUMP(num_mem_chunks > IHK_MAX_NUM_MEM_CHUNKS, -EINVAL, "too many memory chunks requested\n");

	fd = ihklib_os_open(index);
	CHKANDJUMP(fd < 0, -errno, "ihklib_os_open failed\n");

    mem_array2str(mem_list, sizeof(mem_list), num_mem_chunks, mem_chunks);

	req.string = mem_list;
	req.string_len = strlen(mem_list);
	CHKANDJUMP(!req.string || !req.string_len, -EINVAL, "invalid format, string=%s\n", mem_list);

	ret_ioctl = ioctl(fd, IHK_OS_ASSIGN_MEM, &req);
	CHKANDJUMP(ret_ioctl != 0, -errno, "ioctl failed, list=%s\n", mem_list);

 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}

int ihk_os_get_num_assigned_mem_chunks(int index)
{
	int ret = 0, ret_ioctl, ret_ihklib; 
    char result[16 * IHK_MAX_NUM_MEM_CHUNKS];
	struct ihk_mem_chunk mem_chunks[IHK_MAX_NUM_MEM_CHUNKS];
	int num_mem_chunks = 0;
	int fd = -1;

    fd = ihklib_os_open(index);
    CHKANDJUMP(fd < 0, -errno, "ihklib_os_open failed\n");

	memset(result, 0, sizeof(result));

	ret_ioctl = ioctl(fd, IHK_OS_QUERY_MEM, result);
    CHKANDJUMP(ret_ioctl != 0, -errno, "ioctl failed\n");
	dprintf("%s,result=%s\n", __FUNCTION__, result);

	ret_ihklib = mem_str2array(result, &num_mem_chunks, mem_chunks);
	CHKANDJUMP(ret_ihklib != 0, -EINVAL, "mem_str2array failed\n");
	dprintf("%s,def,num_mem_chunks=%d\n", __FUNCTION__, num_mem_chunks);
	
	ret = num_mem_chunks;

 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}

int ihk_os_query_mem(int index, struct ihk_mem_chunk* mem_chunks, int _num_mem_chunks)
{
	int ret = 0, ret_ioctl, ret_ihklib; 
    char result[16 * IHK_MAX_NUM_MEM_CHUNKS];
	int num_mem_chunks = _num_mem_chunks;
	int fd = -1;

    fd = ihklib_os_open(index);
    CHKANDJUMP(fd < 0, -errno, "ihklib_os_open failed\n");

	memset(result, 0, sizeof(result));

	ret_ioctl = ioctl(fd, IHK_OS_QUERY_MEM, result);
    CHKANDJUMP(ret_ioctl != 0, -errno, "ioctl failed\n");
	dprintf("%s,result=%s\n", __FUNCTION__, result);

	ret_ihklib = mem_str2array(result, &num_mem_chunks, mem_chunks);
	CHKANDJUMP(ret_ihklib != 0, -EINVAL, "mem_str2array failed\n");
	dprintf("%s,def,num_mem_chunks=%d\n", __FUNCTION__, num_mem_chunks);
	
	CHKANDJUMP(num_mem_chunks != _num_mem_chunks, -EINVAL, "actual number of memory chunks (%d) is different than requested (%d)\n", num_mem_chunks, _num_mem_chunks);

 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}

int ihk_os_release_mem(int index, struct ihk_mem_chunk* mem_chunks, int num_mem_chunks)
{
	int ret = 0, ret_ioctl;
	struct ihk_ioctl_desc req;
	char mem_list[IHK_MAX_NUM_MEM_CHUNKS];
	int fd = -1;
	
    CHKANDJUMP(num_mem_chunks > IHK_MAX_NUM_MEM_CHUNKS, -EINVAL, "too many memory chunks specified\n");

	fd = ihklib_os_open(index);
    CHKANDJUMP(fd < 0, -errno, "ihklib_os_open failed\n");

	mem_array2str(mem_list, sizeof(mem_list), num_mem_chunks, mem_chunks);

	req.string = mem_list;
	req.string_len = strlen(mem_list);
	CHKANDJUMP(!req.string || !req.string_len, -EINVAL, "invalid format, string=%s\n", mem_list);

	ret_ioctl = ioctl(fd, IHK_OS_RELEASE_MEM, &req);
	CHKANDJUMP(ret_ioctl != 0, -errno, "ioctl failed, string=%s\n", mem_list);

 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}

int ihk_os_get_eventfd(int index, int type)
{
	int fd = -1;
	int ret = 0, ret_ioctl;
	struct ihk_os_ioctl_eventfd_desc desc;
	
	memset(&desc, 0, sizeof(desc));

	fd = ihklib_os_open(index);
	CHKANDJUMP(fd < 0, -errno, "ihklib_os_open failed\n");

	switch (type) {
	case IHK_OS_EVENTFD_TYPE_OOM:
	case IHK_OS_EVENTFD_TYPE_STATUS:
	case IHK_OS_EVENTFD_TYPE_KMSG:
		break;
	default:
		CHKANDJUMP(1, -EINVAL, "unknown type=%d\n", type);
	}
	
	desc.fd = eventfd(0, 0);
	desc.type = type;
	ret_ioctl = ioctl(fd, IHK_OS_REGISTER_EVENT, &desc);
	CHKANDJUMP(ret_ioctl != 0, -errno, "ioctl failed\n");
	
	ret = desc.fd;
 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}

int ihk_os_load(int index, char* fn)
{
	int ret = 0, ret_ioctl;
	int fd = -1;

	fd = ihklib_os_open(index);
	CHKANDJUMP(fd < 0, -errno, "ihklib_os_open failed\n");

	CHKANDJUMP(fn == NULL, -EINVAL, "file name is NULL\n");
	ret_ioctl = ioctl(fd, IHK_OS_LOAD, (unsigned long)fn);
	CHKANDJUMP(ret_ioctl != 0, -errno, "ioctl failed\n");

 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}

int ihk_os_kargs(int index, char* kargs)
{
	int ret = 0, ret_ioctl;
	int fd = -1;

	fd = ihklib_os_open(index);
	CHKANDJUMP(fd < 0, -errno, "ihklib_os_open failed\n");

	ret_ioctl = ioctl(fd, IHK_OS_SET_KARGS, kargs);
    CHKANDJUMP(ret_ioctl != 0, -errno, "ioctl failed\n");
 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}

int ihk_os_boot(int index)
{
	int ret = 0, ret_ioctl;
	int fd = -1;

	fd = ihklib_os_open(index);
	CHKANDJUMP(fd < 0, -errno, "ihklib_os_open failed\n");

	ret_ioctl = ioctl(fd, IHK_OS_BOOT, 0);
	CHKANDJUMP(ret_ioctl != 0, -errno, "ioctl failed\n");

 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}

int ihk_os_shutdown(int index)
{
	int ret = 0, ret_ioctl;
	int fd = -1;

	fd = ihklib_os_open(index);
	CHKANDJUMP(fd < 0, -errno, "ihklib_os_open failed\n");

	ret_ioctl = ioctl(fd, IHK_OS_SHUTDOWN, 0);
    CHKANDJUMP(ret_ioctl != 0, -errno, "ioctl failed\n");

 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}

int ihk_os_get_status(int index)
{
	int ret = IHK_STATUS_INACTIVE, ret_ioctl;
	int fd = -1;
	char query_result[1024];

	fd = ihklib_os_open(index);
	CHKANDJUMP(fd < 0, -errno, "ihklib_os_open failed\n");

	memset(query_result, 0, sizeof(query_result));

	ret_ioctl = ioctl(fd, IHK_OS_STATUS, query_result);
	CHKANDJUMP(ret_ioctl < 0, -errno, "ioctl failed\n");

	dprintf("ioctl returns %d\n", ret_ioctl);
	switch (ret_ioctl) {
	case IHK_OS_STATUS_NOT_BOOTED:
		ret = IHK_STATUS_INACTIVE;
		break;
	case IHK_OS_STATUS_BOOTING:
	case IHK_OS_STATUS_BOOTED:
		ret = IHK_STATUS_BOOTING;
		break;
	case IHK_OS_STATUS_READY:
		ret = IHK_STATUS_RUNNING;
		break;
	case IHK_OS_STATUS_SHUTDOWN:
	case IHK_OS_STATUS_STOPPED:
		ret = IHK_STATUS_SHUTDOWN;
		break;
	case IHK_OS_STATUS_FAILED:
		ret = IHK_STATUS_PANIC;
		break;
	case IHK_OS_STATUS_HUNGUP:
		ret = IHK_STATUS_HUNGUP;
		break;
	case IHK_OS_STATUS_FREEZING:
		ret = IHK_STATUS_FREEZING;
		break;
	case IHK_OS_STATUS_FROZEN:
		ret = IHK_STATUS_FROZEN;
		break;
	default:
		CHKANDJUMP(1, -EINVAL, "unknown os status=%d\n", ret);
		break;
	}

 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}

int ihk_os_create_pseudofs(int index)
{
	int ret = 0, status;
	uid_t euid;
	int fd = -1;

	fd = ihklib_os_open(index);
	CHKANDJUMP(fd < 0, -errno, "ihklib_os_open failed\n");

	euid = geteuid();
	CHKANDJUMP(euid != 0, -EPERM, "only root is allowed to call this function\n");
	
	status = system("/bin/bash " SBINDIR "/mcoverlay-create.sh");
	CHKANDJUMP(status == -1 || WEXITSTATUS(status) != 0, -EINVAL, "system failed\n");

 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}

int ihk_os_destroy_pseudofs(int index)
{
	int ret = 0, status;
	uid_t euid;
	int fd = -1;

	euid = geteuid();
	CHKANDJUMP(euid != 0, -EPERM, "only root is allowed to call this function\n");
	
	status = system("/bin/bash " SBINDIR "/mcoverlay-destroy.sh");
	CHKANDJUMP(status == -1 || WEXITSTATUS(status) != 0, -EINVAL, "system failed\n");

 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}

int ihk_os_get_kmsg_size(int index)
{
	int ret = 0;
	int fd = -1;

	fd = ihklib_os_open(index);
	CHKANDJUMP(fd < 0, -errno, "ihklib_os_open failed\n");
	
	ret = IHK_KMSG_SIZE;

 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}

int ihk_os_kmsg(int index, char* kmsg, ssize_t sz_kmsg)
{
	int ret = 0;
	int fd = -1;

	fd = ihklib_os_open(index);
	CHKANDJUMP(fd < 0, -errno, "ihklib_os_open failed\n");

	CHKANDJUMP(sz_kmsg > IHK_KMSG_SIZE, -EINVAL, "message size is too large\n");

	ret = ioctl(fd, IHK_OS_READ_KMSG, (unsigned long)kmsg);
    CHKANDJUMP(ret < 0, -errno, "ioctl failed\n");

 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}

int ihk_os_clear_kmsg(int index)
{
	int ret = 0, ret_ioctl;
	int fd = -1;

	fd = ihklib_os_open(index);
	CHKANDJUMP(fd < 0, -errno, "ihklib_os_open failed\n");

	ret_ioctl = ioctl(fd, IHK_OS_CLEAR_KMSG, 0);
    CHKANDJUMP(ret_ioctl != 0, -errno, "ioctl failed\n");

 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}

int ihk_os_get_num_numa_nodes(int index)
{
	int ret = 0, ret_ioctl;
	int fd = -1;

	fd = ihklib_os_open(index);
	CHKANDJUMP(fd < 0, -errno, "ihklib_os_open failed\n");

	ret_ioctl = ioctl(fd, IHK_OS_GET_NUM_NUMA_NODES);
    CHKANDJUMP(ret_ioctl < 0, -errno, "ioctl failed\n");

	ret = ret_ioctl;
 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}

int _ihklib_os_query_free_mem(int index, char *result, ssize_t sz_result)
{
	int ret = 0;
	int node = 0;
	char path[PATH_MAX];
	int len = 0;
	struct stat sb;

	memset(result, 0, sz_result);

	snprintf(path, PATH_MAX,
			"/sys/devices/virtual/mcos/mcos%d/"
			"sys/devices/system/node/node%d/meminfo",
			 index, node);

	while (stat(path, &sb) != -1) {
		unsigned long free_kb = 0;
		FILE *f = fopen(path, "r");
		char *line = NULL;
		size_t line_len;

		CHKANDJUMP(!f, -1, "error: opening %s\n", path);

		while (getline(&line, &line_len, f) != -1) {
			int scan_node;
			if (sscanf(line, "Node %d MemFree:%16lu kB",
						&scan_node, &free_kb) == 2) {
				if (node > 0)
					len += snprintf(&result[len], sz_result - len, ",");

				len += snprintf(&result[len], sz_result - len,
						"%lu@%d",
						free_kb * 1024, node);
			}

			free(line);
			line = NULL;
		}

		++node;
		snprintf(path, PATH_MAX,
				"/sys/devices/virtual/mcos/mcos%d/"
				"sys/devices/system/node/node%d/meminfo",
				index, node);
		fclose(f);
	}
	
	CHKANDJUMP(len == 0, -1, "MemFree not found\n");

	dprintf("%s,result=%s\n", __FUNCTION__, result);

 out:
	return ret;
}

int ihk_os_query_free_mem(int index, unsigned long *memfree, int num_numa_nodes)
{
	int i, ret = 0, ret_internal;
    char result[16 * IHK_MAX_NUM_NUMA_NODES];
	struct ihk_mem_chunk mem_chunks[IHK_MAX_NUM_NUMA_NODES];
	int num_mem_chunks = num_numa_nodes;
	int fd = -1;

	fd = ihklib_os_open(index);
	CHKANDJUMP(fd < 0, -errno, "ihklib_os_open failed\n");

	ret_internal = _ihklib_os_query_free_mem(index, result, sizeof(result));
	CHKANDJUMP(ret_internal != 0, -EINVAL, "ihklib_os_query_free_mem failed\n");

	memset(mem_chunks, 0, sizeof(mem_chunks));
	mem_str2array(result, &num_mem_chunks, mem_chunks);

	CHKANDJUMP(num_mem_chunks != num_numa_nodes, -EINVAL, "actual number of NUMA nodes (%d) is different than requested (%d)\n", num_mem_chunks, num_numa_nodes);

	for (i = 0; i < num_mem_chunks; i++) {
		CHKANDJUMP(mem_chunks[i].numa_node_number >= num_numa_nodes || mem_chunks[i].numa_node_number < 0, -EINVAL, "NUMA node number out of range\n");
		memfree[mem_chunks[i].numa_node_number] = mem_chunks[i].size;
	}

 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}

int ihk_os_get_num_pagesizes(int index)
{
	int ret = 0;
	int fd = -1;

	fd = ihklib_os_open(index);
	CHKANDJUMP(fd < 0, -errno, "ihklib_os_open failed\n");

	ret = IHK_NUM_PAGESIZES;

 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}

int ihk_os_get_pagesizes(int index, long *pgsizes, int num_pgsizes)
{
	int ret = 0;
	int i;
	int fd = -1;

	fd = ihklib_os_open(index);
	CHKANDJUMP(fd < 0, -errno, "ihklib_os_open failed\n");

	for (i = 0; i < num_pgsizes; i++) {
		pgsizes[i] = ihk_pgsizes[i];
	}

 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}

#ifdef ENABLE_RUSAGE
int ihk_os_getrusage(int index, void *rusage, size_t size_rusage)
{
	int ret = 0, ret_ioctl;
	struct mcctrl_ioctl_getrusage_desc desc = { .rusage = rusage, .size_rusage = size_rusage };
	int fd = -1;

	fd = ihklib_os_open(index);
	CHKANDJUMP(fd < 0, -errno, "ihklib_os_open failed\n");

	ret_ioctl = ioctl(fd, IHK_OS_GETRUSAGE, &desc);
	CHKANDJUMP(ret_ioctl != 0, -errno, "ioctl failed,ret=%d\n", ret);

 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}
#else
int ihk_os_getrusage(int index, void *rusage, size_t size_rusage)
{
	eprintf("Specify --enable-rusage when configuring.\n");
	return -ENOSYS;
}
#endif

int ihk_os_setperfevent(int index, ihk_perf_event_attr *attr, int n)
{
	int ret = 0, ret_ioctl;
	int fd = -1;
#ifdef POSTK_DEBUG_TEMP_FIX_80 /* ihk_os_setperfevent return value fix. */
	int register_count = 0;
#endif /* POSTK_DEBUG_TEMP_FIX_80 */

	fd = ihklib_os_open(index);
	CHKANDJUMP(fd < 0, -errno, "ihklib_os_open failed\n");

	ret_ioctl = ioctl(fd, IHK_OS_AUX_PERF_NUM, n);
	CHKANDJUMP(ret_ioctl != 0, -errno, "ioctl failed\n");

	ret_ioctl = ioctl(fd, IHK_OS_AUX_PERF_SET, attr);
	CHKANDJUMP(ret_ioctl < 0, -errno, "ioctl failed\n");

#ifdef POSTK_DEBUG_TEMP_FIX_80 /* ihk_os_setperfevent return value fix. */
	register_count = ret_ioctl;

	ret_ioctl = ioctl(fd, IHK_OS_AUX_PERF_NUM, register_count);
	CHKANDJUMP(ret_ioctl != 0, -errno, "ioctl failed\n");

	ret = register_count;
#else /* POSTK_DEBUG_TEMP_FIX_80 */
	ret = ret_ioctl;
#endif /* POSTK_DEBUG_TEMP_FIX_80 */
 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}

int ihk_os_perfctl(int index, int comm)
{
	int ret = 0, ret_ioctl;
	int fd = -1;

	fd = ihklib_os_open(index);
	CHKANDJUMP(fd < 0, -errno, "ihklib_os_open failed\n");

	switch (comm) {
	case PERF_EVENT_ENABLE : /* start PA event */
		ret_ioctl = ioctl(fd, IHK_OS_AUX_PERF_ENABLE, 0);
		break;
	case PERF_EVENT_DISABLE : /* stop PA event */
		ret_ioctl = ioctl(fd, IHK_OS_AUX_PERF_DISABLE, 0);
		break;
	case PERF_EVENT_DESTROY : /* delete PA event */
		ret_ioctl = ioctl(fd, IHK_OS_AUX_PERF_DESTROY, 0);
		break;
	default:
		return(-EINVAL);
	}
	CHKANDJUMP(ret_ioctl != 0, -errno, "ioctl failed\n");

 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}

int ihk_os_getperfevent(int index, unsigned long *counter, int n)
{
	int ret = 0, ret_ioctl;
	int fd = -1;

	fd = ihklib_os_open(index);
	CHKANDJUMP(fd < 0, -errno, "ihklib_os_open failed\n");

	ret_ioctl = ioctl(fd, IHK_OS_AUX_PERF_GET, counter);
	CHKANDJUMP(ret_ioctl != 0, -errno, "ioctl failed\n");

 out:
    if (fd != -1) {
        close(fd);
    }
	return ret;
}

int ihk_os_freeze(unsigned long *os_set, int n)
{
	int ret = 0, ret_ioctl;
	int index;
	int fd = -1;

	for (index = 0; index < n; index++) {
		if (*(os_set + index / 64) & (1ULL << (index % 64))) {
			fd = ihklib_os_open(index);
			CHKANDJUMP(fd < 0, -errno, "ihklib_os_open failed\n");

			ret_ioctl = ioctl(fd, IHK_OS_FREEZE, 0);
			CHKANDJUMP(ret_ioctl != 0, -errno, "ioctl failed\n");

			close(fd);
			fd = -1;
		}
    }
 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}

int ihk_os_thaw(unsigned long *os_set, int n)
{
	int ret = 0, ret_ioctl;
	int index;
	int fd = -1;

	for (index = 0; index < n; index++) {
		if (*(os_set + index / 64) & (1ULL << (index % 64))) {
			fd = ihklib_os_open(index);
			CHKANDJUMP(fd < 0, -errno, "ihklib_os_open failed\n");

			ret_ioctl = ioctl(fd, IHK_OS_THAW, 0);
			CHKANDJUMP(ret_ioctl != 0, -errno, "ioctl failed\n");

			close(fd);
			fd = -1;
		}
	}
 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}

#ifdef ENABLE_MEMDUMP
#include <bfd.h>
#include <inttypes.h>
#include <time.h>
#include <limits.h>
#include <pwd.h>

int ihk_os_makedumpfile(int index, char *dump_file, int dump_level, int interactive)
{
	int ret = 0;
	static char hname[HOST_NAME_MAX+1];
	bfd *abfd = NULL;
	bfd_boolean ok;
	asection *scn;
	dumpargs_t args;
	unsigned long phys_size, phys_offset;
	int error, i;
	size_t bsize;
	void *buf = NULL;
	uintptr_t addr;
	size_t cpsize;
	time_t t;
	struct tm *tm;
	char *date;
	struct passwd *pw;
	dump_mem_chunks_t *mem_chunks;
	long mem_size;
	char *physmem_name_buf = NULL;
	char physmem_name[PHYSMEM_NAME_SIZE];
	int osfd = -1;

	dprintf("%s: index=%d,dump_file=%s,dump_level=%d,interactive=%d\n", __FUNCTION__, index, dump_file, dump_level, interactive);

	osfd = ihklib_os_open(index);
	CHKANDJUMP(osfd < 0, -errno, "ihklib_os_open failed\n");

	t = time(NULL);
	CHKANDJUMP(t == (time_t)-1, -errno, "time failed: %s\n", strerror(errno));

	tm = localtime(&t);
	CHKANDJUMP(tm == NULL, -EINVAL, "localtime failed\n");

	error = gethostname(hname, sizeof(hname));
	CHKANDJUMP(error != 0, -errno, "gethostname failed\n");

	pw = getpwuid(getuid());
	CHKANDJUMP(pw == NULL, -errno, "getpwuid failed: %s\n", strerror(errno));

	args.cmd = DUMP_SET_LEVEL;
	args.level = dump_level;
	error = ioctl(osfd, IHK_OS_DUMP, &args);
	CHKANDJUMP(error != 0, -errno, "DUMP_SET_LEVEL failed\n");

	args.cmd = DUMP_NMI;
	error = ioctl(osfd, IHK_OS_DUMP, &args);
	CHKANDJUMP(error != 0, -errno, "DUMP_NMI failed\n");

	args.cmd = DUMP_QUERY_NUM_MEM_AREAS;
	args.size = 0;
	error = ioctl(osfd, IHK_OS_DUMP, &args);
	CHKANDJUMP(error != 0, -errno, "DUMP_QUERY_NUM_MEM_AREAS failed\n");

	mem_size = args.size;
	mem_chunks = malloc(mem_size);
	CHKANDJUMP(mem_chunks == NULL, -ENOMEM, "malloc failed\n");

	memset(mem_chunks, 0, args.size);

	args.cmd = DUMP_QUERY_MEM_AREAS;
	args.buf = (void *)mem_chunks;
	error = ioctl(osfd, IHK_OS_DUMP, &args);
	CHKANDJUMP(error != 0, -errno, "DUMP_QUERY_MEM_AREAS failed\n");

	phys_size = 0;
	dprintf("%s: nr chunks: %d\n", __FUNCTION__, mem_chunks->nr_chunks);
	for (i = 0; i < mem_chunks->nr_chunks; ++i) {
		dprintf("%s: 0x%lx:%lu\n",
				__FUNCTION__,
				mem_chunks->chunks[i].addr,
				mem_chunks->chunks[i].size);
		phys_size += mem_chunks->chunks[i].size;
	}

	bsize = 0x100000;
	buf = malloc(bsize);
	CHKANDJUMP(buf == NULL, -ENOMEM, "malloc failed\n");

	bfd_init();

#ifdef POSTK_DEBUG_ARCH_DEP_34
	abfd = bfd_fopen(dump_file, NULL, "w", -1);
#else	/* POSTK_DEBUG_ARCH_DEP_34 */
	abfd = bfd_fopen(dump_file, "elf64-x86-64", "w", -1);
#endif	/* POSTK_DEBUG_ARCH_DEP_34 */

	CHKANDJUMP(abfd == NULL, -EINVAL, "bfd_fopen failed: %s\n", bfd_errmsg(bfd_get_error()));

	ok = bfd_set_format(abfd, bfd_object);
	CHKANDJUMP(!ok, -EINVAL, "bfd_set_format failed: %s\n", bfd_errmsg(bfd_get_error()));

	date = asctime(tm);
	if (date) {
		cpsize = strlen(date) - 1;	/* exclude trailing '\n' */
		scn = bfd_make_section_anyway(abfd, "date");
		CHKANDJUMP(!scn, -EINVAL, "bfd_make_section_anyway(date) failed: %s\n", bfd_errmsg(bfd_get_error()));

		ok = bfd_set_section_size(abfd, scn, cpsize);
		CHKANDJUMP(!ok, -EINVAL, "bfd_set_section_size failed: %s\n", bfd_errmsg(bfd_get_error()));

		ok = bfd_set_section_flags(abfd, scn, SEC_HAS_CONTENTS);
		CHKANDJUMP(!ok, -EINVAL, "bfd_set_setction_flags failed: %s\n", bfd_errmsg(bfd_get_error()));
	}
	error = gethostname(hname, sizeof(hname));
	if (!error) {
		cpsize = strlen(hname);
		scn = bfd_make_section_anyway(abfd, "hostname");
		CHKANDJUMP(!scn, -EINVAL, "bfd_make_section_anyway(hostname) failed: %s\n", bfd_errmsg(bfd_get_error()));

		ok = bfd_set_section_size(abfd, scn, cpsize);
		CHKANDJUMP(!ok, -EINVAL, "bfd_set_section_size failed: %s\n", bfd_errmsg(bfd_get_error()));

		ok = bfd_set_section_flags(abfd, scn, SEC_HAS_CONTENTS);
		CHKANDJUMP(!ok, -EINVAL, "bfd_set_setction_flags failed: %s\n", bfd_errmsg(bfd_get_error()));
	}
	pw = getpwuid(getuid());
	if (pw) {
		cpsize = strlen(pw->pw_name);
		scn = bfd_make_section_anyway(abfd, "user");
		CHKANDJUMP(!scn, -EINVAL, "bfd_make_section_anyway(user) failed: %s\n", bfd_errmsg(bfd_get_error()));

		ok = bfd_set_section_size(abfd, scn, cpsize);
		CHKANDJUMP(!ok, -EINVAL, "bfd_set_section_size failed: %s\n", bfd_errmsg(bfd_get_error()));

		ok = bfd_set_section_flags(abfd, scn, SEC_HAS_CONTENTS);
		CHKANDJUMP(!ok, -EINVAL, "bfd_set_setction_flags failed: %s\n", bfd_errmsg(bfd_get_error()));
	}

	/* Add section for physical memory chunks information */
	scn = bfd_make_section_anyway(abfd, "physchunks");
	CHKANDJUMP(!scn, -EINVAL, "bfd_make_section_anyway(physchunks) failed: %s\n", bfd_errmsg(bfd_get_error()));

	ok = bfd_set_section_size(abfd, scn, mem_size);
	CHKANDJUMP(!ok, -EINVAL, "bfd_set_section_size failed: %s\n", bfd_errmsg(bfd_get_error()));

	ok = bfd_set_section_flags(abfd, scn, SEC_ALLOC|SEC_HAS_CONTENTS);
	CHKANDJUMP(!ok, -EINVAL, "bfd_set_setction_flags failed: %s\n", bfd_errmsg(bfd_get_error()));

	for (i = 0; i < mem_chunks->nr_chunks; ++i) {

		physmem_name_buf = malloc(PHYSMEM_NAME_SIZE);
		memset(physmem_name_buf,0,PHYSMEM_NAME_SIZE);
		sprintf(physmem_name_buf, "physmem%d",i);

		/* Physical memory contents section */
		scn = bfd_make_section_anyway(abfd, physmem_name_buf);
		CHKANDJUMP(!scn, -EINVAL, "bfd_make_section_anyway(physmem) failed: %s\n", bfd_errmsg(bfd_get_error()));

		if (interactive) {
			ok = bfd_set_section_size(abfd, scn, 4096);
		}
		else {
			ok = bfd_set_section_size(abfd, scn, mem_chunks->chunks[i].size);
		}
	
		CHKANDJUMP(!ok, -EINVAL, "bfd_set_section_size failed: %s\n", bfd_errmsg(bfd_get_error()));

		ok = bfd_set_section_flags(abfd, scn, SEC_ALLOC|SEC_HAS_CONTENTS);
		CHKANDJUMP(!ok, -EINVAL, "bfd_set_setction_flags failed: %s\n", bfd_errmsg(bfd_get_error()));

		scn->vma = mem_chunks->chunks[i].addr;

	}

	scn = bfd_get_section_by_name(abfd, "date");
	if (scn) {
		ok = bfd_set_section_contents(abfd, scn, date, 0, scn->size);
		CHKANDJUMP(!ok, -EINVAL, "bfd_set_section_contents(date) failed: %s\n", bfd_errmsg(bfd_get_error()));
	}

	scn = bfd_get_section_by_name(abfd, "hostname");
	if (scn) {
		ok = bfd_set_section_contents(abfd, scn, hname, 0, scn->size);
		CHKANDJUMP(!ok, -EINVAL, "bfd_set_section_contents(hostname) failed: %s\n", bfd_errmsg(bfd_get_error()));
	}

	scn = bfd_get_section_by_name(abfd, "user");
	if (scn) {
		ok = bfd_set_section_contents(abfd, scn, pw->pw_name, 0, scn->size);
		CHKANDJUMP(!ok, -EINVAL, "bfd_set_section_contents(user) failed: %s\n", bfd_errmsg(bfd_get_error()));
	}

	scn = bfd_get_section_by_name(abfd, "physchunks");
	if (scn) {
		ok = bfd_set_section_contents(abfd, scn, mem_chunks, 0, mem_size);
		CHKANDJUMP(!ok, -EINVAL, "bfd_set_section_contents(physchunks) failed: %s\n", bfd_errmsg(bfd_get_error()));
	}

	if (interactive)
		goto out;

	for (i = 0; i < mem_chunks->nr_chunks; ++i) {

		phys_offset = 0;

		memset(physmem_name,0,sizeof(physmem_name));
		sprintf(physmem_name, "physmem%d",i);

		scn = bfd_get_section_by_name(abfd, physmem_name);
		CHKANDJUMP(!scn, -EINVAL, "err bfd_get_section_by_name(physmem_name) failed: %s\n", bfd_errmsg(bfd_get_error()));

		for (addr = mem_chunks->chunks[i].addr;
				addr < (mem_chunks->chunks[i].addr + mem_chunks->chunks[i].size);
				addr += cpsize) {

			cpsize = (mem_chunks->chunks[i].addr + mem_chunks->chunks[i].size) - addr;
			if (cpsize > bsize) {
				cpsize = bsize;
			}

			args.cmd = DUMP_READ;
			args.start = addr;
			args.size = cpsize;
			args.buf = buf;

			error = ioctl(osfd, IHK_OS_DUMP, &args);
			CHKANDJUMP(error, -errno, "DUMP_READ failed\n");

			ok = bfd_set_section_contents(abfd, scn, buf, phys_offset, cpsize);
			CHKANDJUMP(!ok, -EINVAL, "bfd_set_section_contents(physmem) failed: %s\n", bfd_errmsg(bfd_get_error()));

			phys_offset += cpsize;
		}
	}

out:
	if (abfd >= 0) {
		ok = bfd_close(abfd);
		if (!ok) {
			eprintf("bfd_close failed: %s\n", bfd_errmsg(bfd_get_error()));
			ret = -EINVAL;
		}
	}
	if (osfd >= 0) {
		error = close(osfd);
		if (error) {
			eprintf("close failed: %s\n", strerror(errno));
			ret = -errno;
		}
	}
	return ret;
}
#else /* ENABLE_MEMDUMP */
int ihk_os_makedumpfile(int index, char *dump_file, int dump_level, int interactive)
{
	fprintf(stderr, "dump is not supported.\n");
	return -ENOSYS;
}
#endif /* ENABLE_MEMDUMP */
