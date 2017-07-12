/**
 * \file ihklib.c
 */
#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include "ihk/ihk_host_user.h"
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include "ihklib.h"
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
#include <pthread.h>

int __argc;
char **__argv;

//#define DEBUG
#define MILLI_SEC 1000000

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

#define CHKANDJUMP(cond, err, ...)										\
	do {																\
		if(cond) {														\
			eprintf(__VA_ARGS__);										\
			ret = err;													\
			goto fn_fail;												\
		}																\
	} while(0)

cpu_set_t cpu_set;
char delim[] = " @,";
const char delim1[] =" ,";

int set_cpu(char *cpustr);
int count_numa_node();
int ihk_geteventfd(int index, int type);
int read_sysfs_simple_val(char* sysfs_path,unsigned long* val);
int read_sysfs_key_val(char* sysfs_path,char *keyword, unsigned long* val);

typedef struct {
	int efd;
} check_param;

int set_cpu(char *cpustr) {
	char str_start[1024];
	char str_end[1024];
	int i=0;
	int j=0;
	int startflag=1;
	for (i = 0; i < strlen(cpustr)+1 ; i++) {
		if (*(cpustr+i) == '-') {
			startflag=0;
			str_start[i]='\0';
			j = 0;
		}
		else if (startflag == 1) {
			str_start[i] = *(cpustr+i);
		}
		else if (startflag == 0) {
			str_end[j] = *(cpustr+i);
			j++;
		}	
	}
	if (startflag == 1) {
		CPU_SET(atoi(str_start),&cpu_set);
	} 
	else {
		for (i = atoi(str_start); i <= atoi(str_end); i++) {
			CPU_SET(i,&cpu_set);
		}	
	}
	return (0);
}

static void *check_status (void *p) {
	/* check os status */
	int fd = 0;
	char fn[128];
	int ret;
	char query_result[1024];
	struct timespec req = {0, 100 * MILLI_SEC};
	int efd = ((check_param *)p)->efd;

	memset(query_result, 0, sizeof(query_result));
	sprintf(fn, "/dev/mcos%d", 0);
	fd = open(fn, O_RDONLY);
	if (fd < 0) {
 		perror("open");
		return NULL;
	} 
	for (;;) {
		ret = ioctl(fd, IHK_OS_STATUS, query_result);
		switch (ret) {
		case IHK_OS_STATUS_NOT_BOOTED:
		case IHK_OS_STATUS_BOOTING:
		case IHK_OS_STATUS_BOOTED:
		case IHK_OS_STATUS_READY:
		case IHK_OS_STATUS_SHUTDOWN:
		case IHK_OS_STATUS_STOPPED:
			break;
		case IHK_OS_STATUS_FAILED:
		case IHK_OS_STATUS_HUNGUP:
			ioctl(fd, IHK_OS_EVENTFD, efd);
			return NULL;
			break;
		default:
			break;
		}
		nanosleep(&req, NULL);
	}
}

int ihk_geteventfd(int index, int type) {
	char fn[128];
	int fd = 0;
	int ret = 0;
	unsigned long efd;
	pthread_t thread_id;
	check_param cparam;

	dprintf("ihk geteventfd \n");
	sprintf(fn, "/dev/mcos%d", index);
	fd = open(fn, O_RDWR);
	if (fd < 0) {
		perror("open");
		return (-ENOENT);
	} 
	else {
		switch (type) {
		    case 0:
			// resource type 0: physical memory 
			efd = eventfd(0, 0);
			ret = ioctl(fd, IHK_OS_REGISTER_EVENT, efd);
			break;
		    case 1:
			// resource type 1:  os status
			efd = eventfd(0, 0);
			cparam.efd = efd;
			ioctl(fd, IHK_OS_REGISTER_EVENT, efd | (1L << 32));
			ret = pthread_create(&thread_id, NULL, check_status,
			                     (void *)&cparam);
			break;
		    default:
				return(-EINVAL);
		}
		if (ret != 0) {
			fprintf(stderr, "error: querying eventfd\n");
			return (-EINVAL);
		}
	}
	return efd;
}

int ihk_getoslist(int oslist[], int n) {
	int ret = 0;
	DIR *dir = NULL;
	struct dirent *dirp;
	int oscount = 0;
	char str_nodenum[5];

	dir = opendir(PATH_DEV);
	CHKANDJUMP(dir == NULL, -EINVAL, "opendir failed\n");
	while (dir && (dirp = readdir(dir))) {
		if ((strncmp(dirp->d_name,"mcos",4) == 0)) {
			oscount++;
			if (oscount <= n) {
				strncpy(str_nodenum,dirp->d_name+4, strlen(dirp->d_name)-3);
				oslist[oscount-1] = atoi(str_nodenum);
				dprintf("oscount=%d,oslist[%d]=%d,str_nodenum=%s\n", oscount, oscount-1, oslist[oscount-1], str_nodenum);
			} 
		}
	}
	ret = oscount;
 fn_exit:
	if(dir != NULL) {
		closedir(dir);
	}
	return ret;
 fn_fail:
	goto fn_exit;
}

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

 fn_exit:
	return ret;
 fn_fail:
	goto fn_exit;
}

static void mem_array2str(char* mem_list, ssize_t sz_mem_list, int num_mem_chunks, ihk_mem_chunk* mem_chunks) {
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

static int mem_str2array(char* mem_list, int *num_mem_chunks, ihk_mem_chunk* mem_chunks) {
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
	//fn_exit:
    return ret;
	//fn_fail:
    //goto fn_exit;
}

static int ihklib_device_create_os(int fd)
{
	int ret = 0, ret_ioctl; 
	
	ret_ioctl = ioctl(fd, IHK_DEVICE_CREATE_OS, 0);
	CHKANDJUMP(ret_ioctl != 0, -1, "ioctl failed\n");
 fn_exit:
	return ret;
 fn_fail:
	goto fn_exit;
}

static int ihklib_device_destroy_os(int fd, int os_index)
{
	int ret = 0, ret_ioctl;

	ret_ioctl = ioctl(fd, IHK_DEVICE_DESTROY_OS, os_index);
	CHKANDJUMP(ret_ioctl != 0, -1, "ioctl failed\n");
 fn_exit:
	return ret;
 fn_fail:
	goto fn_exit;
}

static int ihklib_device_reserve_cpu(int fd, int num_cpus, int* cpus)
{
	int ret = 0, ret_ioctl;
	ihk_resource_req_t req;
	char cpu_list[2048];

	cpus_array2str(cpu_list, sizeof(cpu_list), num_cpus, cpus);

	req.string = cpu_list;
	req.string_len = strlen(cpu_list);
	CHKANDJUMP(!req.string || !req.string_len, -EINVAL, "invalid format, string=%s\n", cpu_list);

	ret_ioctl = ioctl(fd, IHK_DEVICE_RESERVE_CPU, &req);
	CHKANDJUMP(ret_ioctl != 0, -EINVAL, "ioctl failed, string=%s\n", cpu_list);
	
 fn_exit:
	return ret;
 fn_fail:
	goto fn_exit;
}

static int ihklib_device_reserve_mem(int fd, int num_mem_chunks, ihk_mem_chunk* mem_chunks)
{
	int ret = 0, ret_ioctl;
	ihk_resource_req_t req;
	char mem_list[2048];

	mem_array2str(mem_list, sizeof(mem_list), num_mem_chunks, mem_chunks);

	req.string = mem_list;
	req.string_len = strlen(mem_list);
	CHKANDJUMP(!req.string || !req.string_len, -EINVAL, "invalid format, list=%s\n", __FUNCTION__, mem_list);

	ret_ioctl = ioctl(fd, IHK_DEVICE_RESERVE_MEM, &req);
	CHKANDJUMP(ret_ioctl != 0, -EINVAL, "ioctl failed, list=%s\n", mem_list);

 fn_exit:
	return ret;
 fn_fail:
	goto fn_exit;
}

static int ihklib_device_release_cpu(int fd, int num_cpus, int* cpus)
{
	int ret = 0, ret_ioctl;
	ihk_resource_req_t req;
	char cpu_list[2048];

	cpus_array2str(cpu_list, sizeof(cpu_list), num_cpus, cpus);

	req.string = cpu_list;
	req.string_len = strlen(cpu_list);
	CHKANDJUMP(!req.string || !req.string_len, -EINVAL, "invalid format, string=%s\n", cpu_list);

	ret_ioctl = ioctl(fd, IHK_DEVICE_RELEASE_CPU, &req);
	CHKANDJUMP(ret_ioctl != 0, -EINVAL, "ioctl failed, string=%s\n", cpu_list);
	
 fn_exit:
	return ret;
 fn_fail:
	goto fn_exit;
}

static int ihklib_device_release_mem(int fd, int num_mem_chunks, ihk_mem_chunk* mem_chunks)
{
	int ret = 0, ret_ioctl;
	ihk_resource_req_t req;
	char mem_list[2048];

	mem_array2str(mem_list, sizeof(mem_list), num_mem_chunks, mem_chunks);
	
	req.string = mem_list;
	req.string_len = strlen(mem_list);
	CHKANDJUMP(!req.string || !req.string_len, -EINVAL, "invalid format, string=%s\n", mem_list);

	ret_ioctl = ioctl(fd, IHK_DEVICE_RELEASE_MEM, &req);
	CHKANDJUMP(ret_ioctl != 0, -EINVAL, "ioctl failed, string=%s\n", mem_list);
	
 fn_exit:
	return ret;
 fn_fail:
	goto fn_exit;
}

static int ihklib_device_query_cpu(int fd, int* num_cpus, int* cpus)
{
	int ret = 0, ret_ioctl, ret_ihklib; 
    char result[1024];

	memset(result, 0, sizeof(result));

	ret_ioctl = ioctl(fd, IHK_DEVICE_QUERY_CPU, result);
	CHKANDJUMP(ret_ioctl != 0, -1, "ioctl failed\n");
	dprintf("%s,result=%s\n", __FUNCTION__, result);

	ret_ihklib = cpus_str2array(result, num_cpus, cpus);
	CHKANDJUMP(ret_ihklib != 0, -1, "cpus_str2array failed\n");
	dprintf("%s,def,num_cpus=%d\n", __FUNCTION__, *num_cpus);

 fn_exit:
	return ret;
 fn_fail:
	goto fn_exit;
}

static int ihklib_device_query_mem(int fd, int* num_mem_chunks, ihk_mem_chunk* mem_chunks)
{
	int ret = 0, ret_ioctl; 
    char result[65536];

	memset(result, 0, sizeof(result));

	ret_ioctl = ioctl(fd, IHK_DEVICE_QUERY_MEM, result);
    CHKANDJUMP(ret_ioctl != 0, -1, "ioctl failed\n");
	dprintf("%s,result=%s\n", __FUNCTION__, result);

	mem_str2array(result, num_mem_chunks, mem_chunks);

 fn_exit:
	return ret;
 fn_fail:
	goto fn_exit;
}

/* Resource management function for administrator */
int ihk_config(int mcd, int comm, ihkconfig *config) {
	enum ihk_command_type cmd_type;
	char fn[1024];
	int fd = -1;
	struct stat file_stat;
	int ret = 0, ret_ihklib, ret_internal;
	
	cmd_type = comm;

	sprintf(fn, "/dev/mcd%d", mcd);
	ret_internal = stat(fn, &file_stat);
	CHKANDJUMP(ret_internal != 0, -ENOENT, "stat failed,fn=%s\n", fn);
 
	fd = open(fn, O_RDONLY);
	CHKANDJUMP(fd == -1, -EINVAL, "open failed,fn=%s\n", fn);

	switch (cmd_type) {
		case IHK_CONFIG_CREATE:
			dprintf("IHK_CONFIG_CREATE called.\n");
			ret_ihklib = ihklib_device_create_os(fd);
			CHKANDJUMP(ret_ihklib != 0, -EINVAL, "ihklib_device_create failed\n");
			break;
		case IHK_CONFIG_DESTROY:
			dprintf("IHK_CONFIG_DESTROY called.\n");
			ret_ihklib = ihklib_device_destroy_os(fd, config->os_index);
			CHKANDJUMP(ret_ihklib != 0, -EINVAL, "ihklib_device_destroy failed\n");
			break;
		case IHK_CONFIG_RESERVE:
			dprintf("IHK_CONFIG_RESERVE called.\n");
			if (config->resource_type == IHK_RESOURCE_CPU) {
				ret_ihklib = ihklib_device_reserve_cpu(fd, config->num_cpus, config->cpus);
				CHKANDJUMP(ret_ihklib != 0, -EINVAL, "ihklib_device_reserve failed\n");
			} 
			else if (config->resource_type == IHK_RESOURCE_MEM) {
				ret_ihklib = ihklib_device_reserve_mem(fd, config->num_mem_chunks, config->mem_chunks);
				CHKANDJUMP(ret_ihklib != 0, -EINVAL, "ihklib_device_reserve_mem failed\n");
			} else {
				dprintf("Invalid resource_type.\n");
				ret = -EINVAL;
				goto fn_fail;
			}
			break;
		case IHK_CONFIG_RELEASE:
			dprintf("IHK_CONFIG_RELEASE called.\n");
			if (config->resource_type == IHK_RESOURCE_CPU) {
				ret_ihklib = ihklib_device_release_cpu(fd, config->num_cpus, config->cpus);
				CHKANDJUMP(ret_ihklib != 0, -EINVAL, "ihklib_os_release_cpu failed\n");
			} 
			else if (config->resource_type == IHK_RESOURCE_MEM) {
				ret_ihklib = ihklib_device_release_mem(fd, config->num_mem_chunks, config->mem_chunks);
				CHKANDJUMP(ret_ihklib != 0, -EINVAL, "ihklib_os_release_mem failed\n");
			} 
			else {
				dprintf("%s,invalid resource_type=%d\n", __FUNCTION__, config->resource_type);
                ret = -EINVAL;
                goto fn_fail;
			}
			break;
		case IHK_CONFIG_QUERY:
			dprintf("%s,IHK_CONFIG_QUERY\n", __FUNCTION__);
			if (config->resource_type == IHK_RESOURCE_CPU) {
				ret_ihklib = ihklib_device_query_cpu(fd, &config->num_cpus, config->cpus);
				CHKANDJUMP(ret_ihklib != 0, -EINVAL, "ihklib_device_query failed\n");
			} 
			else if (config->resource_type == IHK_RESOURCE_MEM) {
				ret_ihklib = ihklib_device_query_mem(fd, &config->num_mem_chunks, config->mem_chunks);
				CHKANDJUMP(ret_ihklib != 0, -EINVAL, "ihklib_device_query failed\n");
			} 
			else {
				dprintf("%s,invalid resource_type=%d\n", __FUNCTION__, config->resource_type);
				ret = -EINVAL;
				goto fn_fail;
			}
			break;
		default:
			dprintf("%s,unknown command type=%d\n", __FUNCTION__, cmd_type);
			ret = -EINVAL;
			goto fn_fail;
	}
 fn_fail:
	if(fd != -1) {
		close(fd);
	}
	return ret;
}

static int ihklib_os_boot(int fd)
{
	int ret = 0, ret_ihklib;
	ret_ihklib = ioctl(fd, IHK_OS_BOOT, 0);
	CHKANDJUMP(ret_ihklib != 0, -1, "ioctl failed\n");

 fn_exit:
	return ret;
 fn_fail:
	goto fn_exit;
}

static int ihklib_os_load(int fd, char* fn)
{
	int ret = 0, ret_ihklib;

	CHKANDJUMP(fn == NULL, -1, "fn=NULL\n");
	ret_ihklib = ioctl(fd, IHK_OS_LOAD, (unsigned long)fn);
	CHKANDJUMP(ret_ihklib != 0, -1, "ioctl failed\n");

 fn_exit:
	return ret;
 fn_fail:
	goto fn_exit;
}

static int ihklib_os_shutdown(int fd)
{
	int ret = 0, ret_ihklib;

	ret_ihklib = ioctl(fd, IHK_OS_SHUTDOWN, 0);
    CHKANDJUMP(ret_ihklib != 0, -1, "ioctl failed\n");
 fn_exit:
	return ret;
 fn_fail:
	goto fn_exit;
}

static int ihklib_os_assign_cpu(int fd, int num_cpus, int* cpus)
{
	int ret = 0, ret_ioctl;
	char cpu_list[2048];
	ihk_resource_req_t req;

	cpus_array2str(cpu_list, sizeof(cpu_list), num_cpus, cpus);

	req.string = cpu_list;
	req.string_len = strlen(cpu_list);
	CHKANDJUMP(!req.string || !req.string_len, -1, "invalid format, string=%s\n", cpu_list);

	ret_ioctl = ioctl(fd, IHK_OS_ASSIGN_CPU, &req);
	CHKANDJUMP(ret_ioctl != 0, -1, "ioctl failed, string=%s\n", cpu_list);
	
 fn_exit:
	return ret;
 fn_fail:
	goto fn_exit;
}

static int ihklib_os_assign_mem(int fd, int num_mem_chunks, ihk_mem_chunk *mem_chunks)
{
	int ret = 0, ret_ioctl;
	ihk_resource_req_t req;
    char mem_list[2048];

    mem_array2str(mem_list, sizeof(mem_list), num_mem_chunks, mem_chunks);

	req.string = mem_list;
	req.string_len = strlen(mem_list);
	CHKANDJUMP(!req.string || !req.string_len, -1, "invalid format, string=%s\n", mem_list);

	ret_ioctl = ioctl(fd, IHK_OS_ASSIGN_MEM, &req);
	CHKANDJUMP(ret_ioctl != 0, -1, "ioctl failed, string=%s\n", mem_list);

 fn_exit:
	return ret;
 fn_fail:
	goto fn_exit;
}

static int ihklib_os_release_cpu(int fd, int num_cpus, int* cpus)
{
	int ret = 0, ret_ioctl;
	char cpu_list[2048];
	ihk_resource_req_t req;

	cpus_array2str(cpu_list, sizeof(cpu_list), num_cpus, cpus);
	req.string = cpu_list;
	req.string_len = strlen(cpu_list);
	CHKANDJUMP(!req.string || !req.string_len, -1, "invalid format, string=%s\n", cpu_list);

	ret_ioctl = ioctl(fd, IHK_OS_RELEASE_CPU, &req);
	CHKANDJUMP(ret_ioctl != 0, -1, "ioctl failed, string=%s\n", cpu_list);
	
 fn_exit:
	return ret;
 fn_fail:
	goto fn_exit;
}

static int ihklib_os_release_mem(int fd, int num_mem_chunks, ihk_mem_chunk* mem_chunks)
{
	int ret = 0, ret_ioctl;
    char mem_list[2048];
	ihk_resource_req_t req;

	mem_array2str(mem_list, sizeof(mem_list), num_mem_chunks, mem_chunks);

	req.string = mem_list;
	req.string_len = strlen(mem_list);
	CHKANDJUMP(!req.string || !req.string_len, -1, "invalid format, string=%s\n", mem_list);

	ret_ioctl = ioctl(fd, IHK_OS_RELEASE_MEM, &req);
	CHKANDJUMP(ret_ioctl != 0, -1, "ioctl failed, string=%s\n", mem_list);

 fn_exit:
	return ret;
 fn_fail:
	goto fn_exit;
}

static int ihklib_os_query_cpu(int fd, int *num_cpus, int* cpus)
{
	int ret = 0, ret_ioctl, ret_ihklib; 
	char result[1024];
	
	memset(result, 0, sizeof(result));

	ret_ioctl = ioctl(fd, IHK_OS_QUERY_CPU, result);
	CHKANDJUMP(ret_ioctl != 0, -1, "ioctl failed\n");
	dprintf("%s,result=%s\n", __FUNCTION__, result);

	ret_ihklib = cpus_str2array(result, num_cpus, cpus);
	CHKANDJUMP(ret_ihklib != 0, -1, "cpus_str2array failed\n");
	dprintf("%s,def,num_cpus=%d\n", __FUNCTION__, *num_cpus);

 fn_exit:
	return ret;
 fn_fail:
	goto fn_exit;
}

static int ihklib_os_query_mem(int fd, int *num_mem_chunks, ihk_mem_chunk* mem_chunks)
{
	int ret = 0, ret_ioctl; 
    char result[65536];

	memset(result, 0, sizeof(result));

	ret_ioctl = ioctl(fd, IHK_OS_QUERY_MEM, result);
    CHKANDJUMP(ret_ioctl != 0, -1, "ioctl failed\n");
	dprintf("%s,result=%s\n", __FUNCTION__, result);

	mem_str2array(result, num_mem_chunks, mem_chunks);
	
 fn_exit:
	return ret;
 fn_fail:
	goto fn_exit;
}

int _ihklib_os_query_free_mem(int os_index, char *result, ssize_t sz_result)
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
			 os_index, node);

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
				os_index, node);
		fclose(f);
	}
	
	CHKANDJUMP(len == 0, -1, "MemFree not found\n");

	dprintf("%s,result=%s\n", __FUNCTION__, result);

 fn_exit:
	return ret;
 fn_fail:
	goto fn_exit;
}

static int ihklib_os_query_free_mem(int os_index, int *num_mem_chunks, ihk_mem_chunk* mem_chunks)
{
	int ret = 0, ret_internal;
    char result[65536];

	ret_internal = _ihklib_os_query_free_mem(os_index, result, sizeof(result));
	CHKANDJUMP(ret_internal != 0, -1, "_ihklib_os_query_free_mem failed\n");
	mem_str2array(result, num_mem_chunks, mem_chunks);

 fn_exit:
	return ret;
 fn_fail:
	goto fn_exit;
}

static int ihklib_os_kargs(int fd, char* kargs)
{
	int ret = 0, ret_ioctl;

	ret_ioctl = ioctl(fd, IHK_OS_SET_KARGS, kargs);
    CHKANDJUMP(ret_ioctl != 0, -1, "ioctl failed\n");
 fn_exit:
	return ret;
 fn_fail:
	goto fn_exit;
}

static int ihklib_os_kmsg(int fd, char* kmsg, ssize_t sz_kmsg)
{
	int ret = 0, ret_ioctl;
	ret_ioctl = ioctl(fd, IHK_OS_READ_KMSG, (unsigned long)kmsg);
    CHKANDJUMP(ret_ioctl < 0, -1, "ioctl failed\n");
	if(ret_ioctl < sz_kmsg) {
		kmsg[ret_ioctl] = 0;
	}
 fn_exit:
	return ret;
 fn_fail:
	goto fn_exit;
}

static int ihklib_os_clear_kmsg(int fd)
{
	int ret = 0, ret_ioctl;
	ret_ioctl = ioctl(fd, IHK_OS_CLEAR_KMSG, 0);
    CHKANDJUMP(ret_ioctl != 0, -1, "ioctl failed\n");

 fn_exit:
	return ret;
 fn_fail:
	goto fn_exit;
}

static int ihklib_os_ikc_map(int fd, int num_ikc_ssets, int* ikc_sset_sizes, int** ikc_sset_members, int* ikc_map)
{
	int ret = 0, ret_ioctl;
	int i, j;
	char map_str[65536];
    char cpu_str[64];

	memset(map_str, 0, sizeof(map_str));
	for (i = 0; i < num_ikc_ssets; i++) {
		for (j = 0; j < ikc_sset_sizes[i]; j++) {
			snprintf(cpu_str, sizeof(cpu_str), "%d", *(ikc_sset_members[i] + j));
			strcat(map_str, cpu_str);
			if(j != ikc_sset_sizes[i] - 1) {
				strcat(map_str, ",");
			}
		}
		strcat(map_str, ":");
		snprintf(cpu_str, sizeof(cpu_str), "%d", ikc_map[i]);
		strcat(map_str, cpu_str);
		if(i != num_ikc_ssets - 1) {
			strcat(map_str, "+");
		}
	}
	dprintf("%s,map_str=%s\n", __FUNCTION__, map_str);

	ret_ioctl = ioctl(fd, IHK_OS_IKC_MAP, map_str);
    CHKANDJUMP(ret_ioctl != 0, -1, "ioctl failed\n");

 fn_exit:
	return ret;
 fn_fail:
	goto fn_exit;
}

/* execute ihkosctl commamd */
int ihk_osctl(int index, int comm, ihkosctl *ctl) {
	enum ihk_osctl_command_type cmd_type;
	int fd = -1;
	char fn[1024];
	struct stat file_stat;
	int ret = 0, ret_ihklib, ret_internal;

 	dprintf("ihk_osctl,comm=%d\n", comm);

	cmd_type = comm;

	sprintf(fn, "/dev/mcos%d", index);
	ret_internal = stat(fn, &file_stat);
	CHKANDJUMP(ret_internal != 0, -ENOENT, "os instance (/dev/mcos%d) not found\n", index);

		fd = open(fn, O_RDONLY);
		CHKANDJUMP(fd == -1, -EINVAL, "open failed\n");

		switch (cmd_type) {
		    case IHK_OSCTL_LOAD:
				dprintf("IHK_OSCTL_LOAD called.\n");
				ret_ihklib = ihklib_os_load(fd, ctl->image);
				CHKANDJUMP(ret_ihklib != 0, -EINVAL, "ihklib_os_load failed\n");
				break;
		    case IHK_OSCTL_BOOT:
				dprintf("IHK_OSCTL_BOOT called.\n");
				ret_ihklib = ihklib_os_boot(fd);
				CHKANDJUMP(ret_ihklib != 0, -EINVAL, "ihklib_os_boot failed\n");
				break;
		    case IHK_OSCTL_SHUTDOWN:
				dprintf("IHK_OSCTL_SHUTDOWN called.\n");
				ret_ihklib = ihklib_os_shutdown(fd);
				CHKANDJUMP(ret_ihklib != 0, -EINVAL, "ihklib_os_shutdown failed\n");
				break;
		    case IHK_OSCTL_ASSIGN:
				dprintf("IHK_OSCTL_ASSIGN called.\n");
				if (ctl->resource_type == IHK_RESOURCE_CPU) {
					ret_ihklib = ihklib_os_assign_cpu(fd, ctl->num_cpus, ctl->cpus);
					CHKANDJUMP(ret_ihklib != 0, -EINVAL, "ihklib_os_assign_cpu failed\n");
				} 
				else if (ctl->resource_type == IHK_RESOURCE_MEM) {
					ret_ihklib = ihklib_os_assign_mem(fd, ctl->num_mem_chunks, ctl->mem_chunks);
					CHKANDJUMP(ret_ihklib != 0, -EINVAL, "ihklib_os_assign_mem failed\n");
				} 
				else {
					dprintf("%s,invalid resource type=%d\n", __FUNCTION__, ctl->resource_type);
					ret = -EINVAL;
					goto fn_fail;
				}
				break;
		    case IHK_OSCTL_RELEASE:
				dprintf("IHK_OSCTL_RELEASE called.\n");
				if (ctl->resource_type == IHK_RESOURCE_CPU) {
					ret_ihklib = ihklib_os_release_cpu(fd, ctl->num_cpus, ctl->cpus);
					CHKANDJUMP(ret_ihklib != 0, -EINVAL, "ihklib_os_release failed\n");
				} 
				else if (ctl->resource_type == IHK_RESOURCE_MEM) {
					ret_ihklib = ihklib_os_release_mem(fd, ctl->num_mem_chunks, ctl->mem_chunks);
					CHKANDJUMP(ret_ihklib != 0, -EINVAL, "ihklib_os_release failed\n");
				} 
				else {
					dprintf("%s,invalid resource_type=%d\n", __FUNCTION__, ctl->resource_type);
					ret = -EINVAL;
					goto fn_fail;
				}
				break;
		    case IHK_OSCTL_QUERY:
				dprintf("%s,IHK_OSCTL_QUERY\n", __FUNCTION__);
				if (ctl->resource_type == IHK_RESOURCE_CPU) {
					ret_ihklib = ihklib_os_query_cpu(fd, &ctl->num_cpus, ctl->cpus);
					CHKANDJUMP(ret_ihklib != 0, -EINVAL, "ihklib_os_query_cpu failed\n");
				} 
				else if (ctl->resource_type == IHK_RESOURCE_MEM) {
					ret_ihklib = ihklib_os_query_mem(fd, &ctl->num_mem_chunks, ctl->mem_chunks);
					CHKANDJUMP(ret_ihklib != 0, -EINVAL, "ihklib_os_query_mem failed\n");
				} 
				else {
					dprintf("invalid resource_type\n");
					ret = -EINVAL;
					goto fn_fail;
				}
				break;
		    case IHK_OSCTL_QUERY_FREE_MEM:
				dprintf("IHK_OSCTL_QUERY_FREE_MEM called.\n");
				ret_ihklib = ihklib_os_query_free_mem(index, &ctl->num_mem_chunks, ctl->mem_chunks);
				CHKANDJUMP(ret_ihklib != 0, -EINVAL, "ihklib_os_query_free_mem failed\n");
				break;
		    case IHK_OSCTL_KARGS:
				dprintf("IHK_OSCTL_KARGS called.\n");
				ret_ihklib = ihklib_os_kargs(fd, ctl->kargs);
				CHKANDJUMP(ret_ihklib != 0, -EINVAL, "ihklib_os_kargs failed\n");
				break;
		    case IHK_OSCTL_KMSG:
				dprintf("IHK_OSCTL_KMSG called.\n");
				ret_ihklib = ihklib_os_kmsg(fd, ctl->kmsg, ctl->kmsg_size);
				CHKANDJUMP(ret_ihklib != 0, -EINVAL, "ihklib_os_kmsg failed\n");
				break;
		    case IHK_OSCTL_CLEAR_KMSG:
				dprintf("IHK_OSCTL_CLEAR_KMSG called.\n");
				ret_ihklib = ihklib_os_clear_kmsg(fd);
				CHKANDJUMP(ret_ihklib != 0, -EINVAL, "ihklib_os_clear_kmsg failed\n");
				break;
		    case IHK_OSCTL_IKC_MAP:
				dprintf("IHK_OSCTL_IKC_MAP called.\n");
				ret_ihklib = ihklib_os_ikc_map(fd, ctl->num_ikc_ssets, ctl->ikc_sset_sizes, ctl->ikc_sset_members, ctl->ikc_map);
				CHKANDJUMP(ret_ihklib != 0, -EINVAL, "ihklib_os_ikc_map failed\n");
				break;
		    default:
				dprintf("%s, unknown cmdtype=%d\n", __FUNCTION__, cmd_type);
				ret = -EINVAL;
				goto fn_fail;
		}
 fn_fail:
	if(fd != -1) {
		close(fd);
	}
	return ret;
}

int ihk_getihkinfo (ihk_info *info) 
{
	int ret = 0, ret_ihklib;
	int i;
	int fd = -1;
	char fn[128];
	DIR *dir;
	struct dirent *direp;
	int os_count = 0;

	fd = open("/dev/mcd0", O_RDWR);
	CHKANDJUMP(fd < 0, -ENOENT, "open failed,fn=%s,errno=%d\n", fn, errno);

	ret_ihklib = ihklib_device_query_mem(fd, &info->num_reserved_mem_chunks, info->reserved_mem_chunks);
	CHKANDJUMP(ret_ihklib != 0, -EINVAL, "ihklib_device_query failed\n");

	ret_ihklib = ihklib_device_query_cpu(fd, &info->num_reserved_cpus, info->reserved_cpus);
	CHKANDJUMP(ret_ihklib != 0, -EINVAL, "ihklib_device_query failed\n");

	for(i = 0; i < info->num_os_instances; i++) {
		int fd;
		sprintf(fn, "/dev/mcos%d", i);
		fd = open(fn, O_RDONLY);
		CHKANDJUMP(fd == -1, -EINVAL, "open failed\n");

		ret_ihklib = ihklib_os_query_mem(fd, &info->num_assigned_mem_chunks[i], info->assigned_mem_chunks[i]);
		CHKANDJUMP(ret_ihklib != 0, -EINVAL, "ihklib_os_query_mem failed\n");

		ret_ihklib = ihklib_os_query_cpu(fd, &info->num_assigned_cpus[i], info->assigned_cpus[i]);
		CHKANDJUMP(ret_ihklib != 0, -EINVAL, "ihklib_os_query_cpu failed\n");
		
		close(fd);
	}
	
	if ((dir = opendir(PATH_DEV)) == NULL) {
		perror("opendir");
		exit(-1);
	}
	while (dir && (direp = readdir(dir))) {
		if ((strncmp(direp->d_name,"mcos",4) == 0)) {
			dprintf("dir:%s\n",direp->d_name);
			os_count++;
			dprintf("count:%d \n",os_count);
		}
	}
	closedir(dir);
	info->num_os_instances = os_count;

 fn_exit:
	if(fd != -1) {
		close(fd);
	}
	return ret;
 fn_fail:
	goto fn_exit;
}

int ihk_getosinfo(int index, ihk_osinfo *osinfo) {
	int fd = -1;
	char fn[128];
	char query_result[65536];
	int ret = 0, ret_ihklib, ret_ioctl;
	char *token;

	sprintf(fn, "/dev/mcos%d", index);
	fd = open(fn, O_RDWR);
	CHKANDJUMP(fd < 0, -ENOENT, "open failed,fn=%s,errno=%d\n", fn, errno);

	ret_ihklib = ihklib_os_query_mem(fd, &osinfo->num_mem_chunks, osinfo->mem_chunks);
    CHKANDJUMP(ret_ihklib != 0, -EINVAL, "ihklib_os_query_mem failed\n");
	
	ret_ihklib = ihklib_os_query_cpu(fd, &osinfo->num_cpus, osinfo->cpus);
	CHKANDJUMP(ret_ihklib != 0, -EINVAL, "ihklib_os_query_cpu failed\n");
	
	memset(query_result, 0, sizeof(query_result));
	ret_ioctl = ioctl(fd, IHK_OS_STATUS, query_result);

	//dprintf("%s,ihkosctl status returned %d\n", __FILE__, ret_ioctl);
	switch (ret_ioctl) {
	case IHK_OS_STATUS_NOT_BOOTED:
		osinfo->status = IHK_STATUS_INACTIVE;
		break;
	case IHK_OS_STATUS_BOOTING:
	case IHK_OS_STATUS_BOOTED:
		osinfo->status = IHK_STATUS_BOOTING;
		break;
	case IHK_OS_STATUS_READY:
		osinfo->status = IHK_STATUS_RUNNING;
		break;
	case IHK_OS_STATUS_SHUTDOWN:
	case IHK_OS_STATUS_STOPPED:
		osinfo->status = IHK_STATUS_SHUTDOWN;
		break;
	case IHK_OS_STATUS_FAILED:
		osinfo->status = IHK_STATUS_PANIC;
		break;
	case IHK_OS_STATUS_HUNGUP:
		osinfo->status = IHK_STATUS_HUNGUP;
		break;
	case IHK_OS_STATUS_FREEZING:
		osinfo->status = IHK_STATUS_FREEZING;
		break;
	case IHK_OS_STATUS_FROZEN:
		osinfo->status = IHK_STATUS_FROZEN;
		break;
	default:
		dprintf("%s,unknown os status=%d\n", __FILE__, ret);
		ret = -EINVAL;
		goto fn_fail;
	}

	/* query ikc_map */
	dprintf("%s,query_ikc_map\n", __FUNCTION__);
	memset(query_result, 0, sizeof(query_result));

	ret_ioctl = ioctl(fd, IHK_OS_QUERY_IKC_MAP, query_result);
    CHKANDJUMP(ret_ioctl != 0, -EINVAL, "ioctl failed\n");
	
	dprintf("%s,query_ikc_map, ioctl returned %s\n", __FUNCTION__, query_result);
	dprintf("%s,ref,num_ikc_ssets=%d\n", __FUNCTION__, osinfo->num_ikc_ssets);

	int sset_rank = 0;
	char* sset = query_result;
	token = strsep(&sset, "+");
	while (token != NULL) {	
		if(*token == 0) {
			goto empty_pair;
		}
		char* cdr = token;
		token = strsep(&cdr, ":");
		CHKANDJUMP(*token == 0, -EINVAL, "query_ikc_map,empty sender set,str=%s\n", query_result);
		CHKANDJUMP(cdr == NULL, -EINVAL, "query_ikc_map,colon not found,str=%s\n", query_result);
		if(osinfo->num_ikc_ssets > sset_rank) {
			osinfo->ikc_map[sset_rank] = atol(cdr);
			dprintf("ikc_map[%d]=%d\n", sset_rank, osinfo->ikc_map[sset_rank]);
		}

		int cpu_rank = 0;
		char* cpus = token;
		token = strsep(&cpus, ",");
		while (token != NULL) {
			if(*token == 0) {
				goto empty_cpu;
			}
			if(osinfo->num_ikc_ssets > sset_rank &&
			   osinfo->ikc_sset_sizes[sset_rank] > cpu_rank) {
				*(osinfo->ikc_sset_members[sset_rank] + cpu_rank) = atol(token);
			}
			cpu_rank++;
		empty_cpu:
			token = strsep(&cpus, ",");
		}
		if(osinfo->num_ikc_ssets > sset_rank) {
			osinfo->ikc_sset_sizes[sset_rank] = cpu_rank;
		}
		sset_rank++;
	empty_pair:
		token = strsep(&sset, "+");
	}
	osinfo->num_ikc_ssets = sset_rank;
	
 fn_exit:
	if(fd != -1) {
		close(fd);
	}
	return ret;
 fn_fail:
	goto fn_exit;
}

#ifdef ENABLE_MEMDUMP
#include <bfd.h>
#include <inttypes.h>
#include <time.h>
#include <limits.h>
#include <pwd.h>

static int do_dump(int osfd, struct ihk_bfd_func *fp) {
	static char path[PATH_MAX];
	static char hname[HOST_NAME_MAX+1];
	bfd *abfd = NULL;
	char *fname;
	bfd_boolean ok;
	asection *scn;
	dumpargs_t args;
	uintptr_t start = 0;
	uintptr_t end = 0;		
	unsigned long phys_size = 0;
	unsigned long phys_offset = 0;
	int error, i;
	size_t bsize;
	void *buf = NULL;
	uintptr_t addr;
	size_t cpsize;
	time_t t;
	struct tm *tm;
	size_t n;
	char *date;
	struct passwd *pw;

	dump_mem_chunks_t *mem_chunks;

#if 0
    char *vmfile; /* file name of kernel image, not used */
#endif
    int opt = 1; /* 0:DUMP_ALL_MEM 1:DUMP_CHUNK_MEM */

	mem_chunks = malloc(PHYS_CHUNKS_DESC_SIZE);
	if (!mem_chunks) {
		perror("allocating mem_chunks buffer: ");
		return 1;
	}

    if (__argc == 6) {
        opt = atoi(__argv[4]);
#if 0
        vmfile = __argv[5];
#endif
    }

	t = time(NULL);
	if (t == (time_t)-1) {
		perror("time");
		return 1;
	}
	tm = localtime(&t);
	if (!tm) {
		perror("localtime");
		return 1;
	}
	gethostname(hname, sizeof(hname));

	pw = getpwuid(getuid());

	args.cmd = DUMP_NMI;
	error = ioctl(osfd, IHK_OS_DUMP, &args);
	if (error) {
		perror("DUMP_NMI");
		return 1;
	}

	switch (opt) { /* switch by option flag */
		case DUMP_ALL_MEM:
    		args.cmd = DUMP_QUERY_ALL;
    		error = ioctl(osfd, IHK_OS_DUMP, &args);
    		if (error) {
        		perror("DUMP_QUERY");
        		return 1;
    		}
    		start = args.start;
    		end = args.start + args.size;
			break;
		case DUMP_CHUNK_MEM:
			args.cmd = DUMP_QUERY;
			args.buf = (void *)mem_chunks;
			error = ioctl(osfd, IHK_OS_DUMP, &args);
			if (error) {
				perror("DUMP_QUERY");
				return 1;
			}
			phys_size = 0;
			dprintf("%s: nr chunks: %d\n", __FUNCTION__, mem_chunks->nr_chunks);
			for (i = 0; i < mem_chunks->nr_chunks; ++i) {
				dprintf("%s: 0x%lx:%lu\n",
					__FUNCTION__,
					mem_chunks->chunks[i].addr,
					mem_chunks->chunks[i].size);
					phys_size += mem_chunks->chunks[i].size;
			}
			break;
		default:
			return 1;
	}	

	bsize = 0x100000;
	buf = malloc(bsize);
	if (!buf) {
		perror("malloc");
		return 1;
	}

	fp->bfd_init();

	if (__argc >= 4) {
		fname = __argv[3];
	}
	else {
		n = strftime(path, sizeof(path), "mcdump_%Y%m%d_%H%M%S", tm);
		if (!n) {
			perror("strftime");
			return 1;
		}
		fname = path;
	}

	abfd = fp->bfd_fopen(fname, "elf64-x86-64", "w", -1);
	if (!abfd) {
		fp->bfd_perror("bfd_fopen");
		return 1;
	}

	ok = fp->bfd_set_format(abfd, bfd_object);
	if (!ok) {
		fp->bfd_perror("bfd_set_format");
		return 1;
	}

	date = asctime(tm);
	if (date) {
		cpsize = strlen(date) - 1;	/* exclude trailing '\n' */
		scn = fp->bfd_make_section_anyway(abfd, "date");
		if (!scn) {
			fp->bfd_perror("bfd_make_section_anyway(date)");
			return 1;
		}

		ok = fp->bfd_set_section_size(abfd, scn, cpsize);
		if (!ok) {
			fp->bfd_perror("bfd_set_section_size");
			return 1;
		}

		ok = fp->bfd_set_section_flags(abfd, scn, SEC_HAS_CONTENTS);
		if (!ok) {
			fp->bfd_perror("bfd_set_setction_flags");
			return 1;
		}
	}
	error = gethostname(hname, sizeof(hname));
	if (!error) {
		cpsize = strlen(hname);
		scn = fp->bfd_make_section_anyway(abfd, "hostname");
		if (!scn) {
			fp->bfd_perror("bfd_make_section_anyway(hostname)");
			return 1;
		}

		ok = fp->bfd_set_section_size(abfd, scn, cpsize);
		if (!ok) {
			fp->bfd_perror("bfd_set_section_size");
			return 1;
		}

		ok = fp->bfd_set_section_flags(abfd, scn, SEC_HAS_CONTENTS);
		if (!ok) {
			fp->bfd_perror("bfd_set_setction_flags");
			return 1;
		}
	}
	pw = getpwuid(getuid());
	if (pw) {
		cpsize = strlen(pw->pw_name);
		scn = fp->bfd_make_section_anyway(abfd, "user");
		if (!scn) {
			fp->bfd_perror("bfd_make_section_anyway(user)");
			return 1;
		}

		ok = fp->bfd_set_section_size(abfd, scn, cpsize);
		if (!ok) {
			fp->bfd_perror("bfd_set_section_size");
			return 1;
		}

		ok = fp->bfd_set_section_flags(abfd, scn, SEC_HAS_CONTENTS);
		if (!ok) {
			fp->bfd_perror("bfd_set_setction_flags");
			return 1;
		}
	}
	if ( opt == DUMP_CHUNK_MEM ) {
	/* Add section for physical memory chunks information */
	scn = fp->bfd_make_section_anyway(abfd, "physchunks");
	if (!scn) {
		fp->bfd_perror("bfd_make_section_anyway(physchunks)");
		return 1;
	}

	ok = fp->bfd_set_section_size(abfd, scn, PHYS_CHUNKS_DESC_SIZE);
	if (!ok) {
		fp->bfd_perror("bfd_set_section_size");
		return 1;
	}

	ok = fp->bfd_set_section_flags(abfd, scn, SEC_ALLOC|SEC_HAS_CONTENTS);
	if (!ok) {
		fp->bfd_perror("bfd_set_setction_flags");
		return 1;
	}
	}
	/* Physical memory contents section */
	scn = fp->bfd_make_section_anyway(abfd, "physmem");
	if (!scn) {
		fp->bfd_perror("bfd_make_section_anyway(physmem)");
		return 1;
	}
	switch (opt) {
		case DUMP_ALL_MEM:
			ok = fp->bfd_set_section_size(abfd, scn, end-start);
			break;
		case DUMP_CHUNK_MEM:
			ok = fp->bfd_set_section_size(abfd, scn, phys_size);
			break;
		default:
			return 1;
	}
	if (!ok) {
		fp->bfd_perror("bfd_set_section_size");
		return 1;
	}

	ok = fp->bfd_set_section_flags(abfd, scn, SEC_ALLOC|SEC_HAS_CONTENTS);
	if (!ok) {
		fp->bfd_perror("bfd_set_setction_flags");
		return 1;
	}
	switch (opt) {
		case DUMP_ALL_MEM:
			scn->vma = start;
			break;
		case DUMP_CHUNK_MEM:
			scn->vma = mem_chunks->chunks[0].addr;
			break;
		default:
			return 1;
	}

	scn = fp->bfd_get_section_by_name(abfd, "date");
	if (scn) {
		ok = fp->bfd_set_section_contents(abfd, scn, date, 0, scn->size);
		if (!ok) {
			fp->bfd_perror("bfd_set_section_contents(date)");
			return 1;
		}
	}

	scn = fp->bfd_get_section_by_name(abfd, "hostname");
	if (scn) {
		ok = fp->bfd_set_section_contents(abfd, scn, hname, 0, scn->size);
		if (!ok) {
			fp->bfd_perror("bfd_set_section_contents(hostname)");
			return 1;
		}
	}

	scn = fp->bfd_get_section_by_name(abfd, "user");
	if (scn) {
		ok = fp->bfd_set_section_contents(abfd, scn, pw->pw_name, 0, scn->size);
		if (!ok) {
			fp->bfd_perror("bfd_set_section_contents(user)");
			return 1;
		}
	}
	switch (opt) {
		case DUMP_ALL_MEM:
    		scn = fp->bfd_get_section_by_name(abfd, "physmem");
    		for (addr = start; addr < end; addr += cpsize) {
        		cpsize = end - addr;
        		if (cpsize > bsize) {
            		cpsize = bsize;
        		}

        		args.cmd = DUMP_READ_ALL;
        		args.start = addr;
        		args.size = cpsize;
        		args.buf = buf;

        		error = ioctl(osfd, IHK_OS_DUMP, &args);
        		if (error) {
            		perror("DUMP_READ");
            		return 1;
        		}
        		ok = fp->bfd_set_section_contents(abfd, scn, buf, addr-start, cpsize);
        		if (!ok) {
            		fp->bfd_perror("bfd_set_section_contents(physmem)");
            		return 1;
        		}
    		}
			break;
		case DUMP_CHUNK_MEM:
			scn = fp->bfd_get_section_by_name(abfd, "physchunks");
			if (scn) {
				ok = fp->bfd_set_section_contents(abfd, scn, mem_chunks, 0, PHYS_CHUNKS_DESC_SIZE);
				if (!ok) {
					fp->bfd_perror("bfd_set_section_contents(physchunks)");
					return 1;
				}
			}
			scn = fp->bfd_get_section_by_name(abfd, "physmem");
			phys_offset = 0;
			for (i = 0; i < mem_chunks->nr_chunks; ++i) {
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
					if (error) {
						perror("DUMP_READ");
						return 1;
					}

				ok = fp->bfd_set_section_contents(abfd, scn, buf, phys_offset, cpsize);
				if (!ok) {
					fp->bfd_perror("bfd_set_section_contents(physmem)");
					return 1;
				}
				phys_offset += cpsize;
				}
			}
			break;
		default:
			return 1;
	};
	ok = fp->bfd_close(abfd);
	if (!ok) {
		fp->bfd_perror("bfd_close");
		return 1;
	}
	return 0;
}
#else /* ENABLE_MEMDUMP */
static int do_dump(int osfd)
{
	fprintf(stderr, "dump is not supported.\n");
	return 1;
}
#endif /* ENABLE_MEMDUMP */

/* count_numa_node */
/* count number of numa nodes */
int count_numa_node () {
	DIR *dir;
	struct dirent *direp;
	int num_numa = 0;
	
	if ((dir = opendir(PATH_SYS_NODE)) == NULL) {
		perror("opendir");
		exit(-EINVAL);
	}
	while (dir && (direp = readdir(dir))) {
		if (strncmp(direp->d_name, "node", 4) == 0 && direp->d_type == DT_DIR) {
			num_numa++;
		}
	}
	closedir(dir);
	return num_numa;
}

#ifdef ENABLE_RUSAGE
struct ihk_os_cpu_monitor {
	int status;
#define IHK_OS_MONITOR_NOT_BOOT 0
#define IHK_OS_MONITOR_IDLE 1
#define IHK_OS_MONITOR_USER 2
#define IHK_OS_MONITOR_KERNEL 3
#define IHK_OS_MONITOR_KERNEL_HEAVY 4
#define IHK_OS_MONITOR_KERNEL_OFFLOAD 5
#define IHK_OS_MONITOR_KERNEL_FREEZING 8
#define IHK_OS_MONITOR_KERNEL_FROZEN 9
#define IHK_OS_MONITOR_KERNEL_THAW 10
#define IHK_OS_MONITOR_PANIC 99
	int status_bak;
	unsigned long counter;
	unsigned long ocounter;
	unsigned long user_tsc;
	unsigned long system_tsc;
};

struct ihk_os_monitor {
	unsigned long rusage_max_num_threads;
	unsigned long rusage_num_threads;
	unsigned long rusage_rss_max;
	unsigned long rusage_rss_current;
	unsigned long rusage_kmem_usage;
	unsigned long rusage_kmem_max_usage;
	unsigned long rusage_hugetlb_usage;
	unsigned long rusage_hugetlb_max_usage;
	unsigned long rusage_total_memory;
	unsigned long rusage_total_memory_usage;
	unsigned long rusage_total_memory_max_usage;
	unsigned long num_numa_nodes;
	unsigned long num_processors;
	unsigned long ns_per_tsc;
	unsigned long reserve[128];
	unsigned long rusage_numa_stat[1024];
};

int
ihk_getrusage(int index, ihk_rusage *rusage)
{
	int fd;
	char path[1024];
	struct ihk_os_monitor monitor;
	struct ihk_os_cpu_monitor *cpu;
	int rc;
	unsigned long *numa_stat = rusage->numa_stat;
	int num_numa_nodes = rusage->num_numa_nodes;
	int i;
	int n;
	unsigned long ut;
	unsigned long st;

	sprintf(path, "/dev/mcos%d", index);	
	if ((fd = open(path, O_RDWR)) == -1) {
		return -errno;
	} 

	if (ioctl(fd, IHK_OS_GET_USAGE, &monitor) == -1) {
		rc = -errno;
		close(fd);
		return rc;
	}

	cpu = malloc(sizeof(struct ihk_os_cpu_monitor) *
	             monitor.num_processors);
	if (!cpu) {
		close(fd);
		return -ENOMEM;
	}

	if (ioctl(fd, IHK_OS_GET_CPU_USAGE, cpu) == -1) {
		rc = -errno;
		close(fd);
		free(cpu);
		return rc;
	}

	memset(numa_stat, '\0', sizeof(unsigned long) * num_numa_nodes);
	memset(rusage, '\0', sizeof(ihk_rusage));
	rusage->numa_stat = numa_stat;

	rusage->rss = monitor.rusage_rss_current;
	rusage->cache = 0;
	rusage->rss_huge = 0;
	rusage->mapped_file = 0;
	rusage->max_usage = monitor.rusage_rss_max;
	rusage->kmem_usage = monitor.rusage_kmem_usage;
	rusage->kmax_usage = monitor.rusage_kmem_max_usage;
	rusage->num_numa_nodes = monitor.num_numa_nodes;
	n = monitor.num_numa_nodes;
	if (n > num_numa_nodes)
		n = num_numa_nodes;
	for (i = 0; i < n; i++)
		rusage->numa_stat[i] = monitor.rusage_numa_stat[i];
	rusage->hugetlb = monitor.rusage_hugetlb_usage;
	rusage->hugetlb_max = monitor.rusage_hugetlb_max_usage;
	for (ut = 0, st = 0, i = 0; i < monitor.num_processors; i++) {
		unsigned long wt;

		wt = cpu[i].user_tsc * monitor.ns_per_tsc / 1000;
		ut += wt;
		st += cpu[i].system_tsc * monitor.ns_per_tsc / 1000;
		rusage->usage_per_cpu[i] = wt;
	}
	rusage->usage = ut;
	rusage->stat_system = st / 10000000;
	rusage->stat_user = ut / 10000000;
	rusage->num_threads = monitor.rusage_num_threads;
	rusage->max_num_threads = monitor.rusage_max_num_threads;

	close(fd);
	free(cpu);
	return 0;
}
#else
/* ihk_getrusage */
int ihk_getrusage(int index, ihk_rusage *rusage) {
	fprintf(stderr, "rusage is not supported.\n");
	return 1;
}
#endif

int read_sysfs_simple_val(char* sysfs_path, unsigned long* val) {
	FILE *fp;
	char buf[1024];
	*val = 0;

	if ((fp = fopen(sysfs_path, "r")) == NULL) {
		perror("fopen");
		return (-1);
	}
	memset(buf, 0, 1024);
	fgets(buf, 1023, fp);
	*val = strtoul(buf, NULL, 0);
	fclose(fp);
	return 0;
}	

int read_sysfs_key_val(char* sysfs_path, char* keyword, unsigned long* val) {
	FILE *fp;
	char buf[1024];
	*val = 0;
	if ((fp = fopen(sysfs_path, "r")) == NULL) {
		perror("fopen");
		return (-1);
	}
	memset(buf, 0, 1024);	
	while (fgets(buf, 1023, fp) != NULL) {
		if (strncmp(buf, keyword, strlen(keyword)) == 0) {
			*val = strtoul(buf+strlen(keyword), NULL, 0);
			return 0;
		}		
		memset(buf, 0, 1024);	
	} 
	fclose(fp);
	return 0;
}
	
/* Create dumpfile of OS instance */
/* ihk_makedumpfile                           */
int __ihk_makedumpfile(int index, char *dumpfile, int opt, char *vmfile, struct ihk_bfd_func *fp)
{
	char fn[1024];
	int ret = 0;
	int fd;
	char optstr[1024];
 	char *args[10];
	__argv = args;
	sprintf(fn, "/dev/mcos%d", index);
	fd = open(fn, O_RDONLY);
	if (fd < 0) {
		perror("open");
		return(-ENOENT);
	}
	__argv[3] = dumpfile;
	sprintf(optstr, "%d", opt);
	__argv[4] = optstr;
	__argv[5] = vmfile;
	__argc = 6;
#ifdef ENABLE_MEMDUMP
	ret = do_dump(fd, fp);
#else
	ret = do_dump(fd);
#endif // ENABLE_MEMDUMP
	return(ret);
}

/* ihk_freeze */
int ihk_freeze(unsigned long *os_set, int n)
{
	char fn[128];
	int ret = 0;
	int fd = 0;
	int i;

	for (i = 0; i < n; i++) {
		if (*(os_set + i / 64) >> (i % 64)) {
			sprintf(fn, "/dev/mcos%d", i);
			fd = open(fn, O_RDONLY);
			CHKANDJUMP(fd < 0, -ENOENT, "open failed\n");

			ret = ioctl(fd, IHK_OS_FREEZE, 0);
			CHKANDJUMP(ret != 0, -EINVAL, "Error: ioctl IHK_OS_FREEZE returned %d\n", ret);
		}
    }
 fn_exit:
	return ret;
 fn_fail:
	goto fn_exit;
}

/* ihk_thaw */
int ihk_thaw(unsigned long *os_set, int n)
{
	char fn[128];
	int ret = 0;
	int fd = 0;
	int i;

	for (i = 0; i < n; i++) {
		if (*(os_set + i / 64) >> (i % 64)) {
			sprintf(fn, "/dev/mcos%d", i);
			fd = open(fn, O_RDONLY);
			CHKANDJUMP(fd < 0, -ENOENT, "open failed\n");
			ret = ioctl(fd, IHK_OS_THAW, 0);
			CHKANDJUMP(ret != 0, -EINVAL, "Error: ioctl IHK_OS_THAW returned %d\n", ret);
		}
	}
 fn_exit:
	return ret;
 fn_fail:
	goto fn_exit;
}

/* ihk_setperfevent */
int ihk_setperfevent (int index, ihk_perf_event_attr *attr, int n)
{
	char fn[128];
	int ret;
	int fd = 0;

	sprintf(fn, "/dev/mcos%d", index);
	fd = open(fn, O_RDONLY);
	if (fd < 0) {
	  ret=-ENOENT;
	}
	else {
	  ret = ioctl(fd, IHK_OS_AUX_PERF_NUM, n);
	  if (ret != 0) {
	    fprintf(stderr, "error: ihk_setperfevent() \n");
	  }
	  ret = ioctl(fd, IHK_OS_AUX_PERF_SET, attr);
	  if (ret < 0) {
	    fprintf(stderr, "error: ihk_setperfevent() \n");
	  }
	}
	return ret;
}

/* ihk_perfctl */
int ihk_perfctl (int index, int comm)
{
	char fn[128];
	int ret;
	int fd = 0;


	sprintf(fn, "/dev/mcos%d", index);
	fd = open(fn, O_RDONLY);
	if (fd < 0) {
	  ret=-ENOENT;
	}
	else {
	  switch (comm) {
	    case PERF_EVENT_ENABLE : /* start PA event */
	      ret = ioctl(fd, IHK_OS_AUX_PERF_ENABLE, 0);
	      break;
	    case PERF_EVENT_DISABLE : /* stop PA event */
	      ret = ioctl(fd, IHK_OS_AUX_PERF_DISABLE, 0);
	      break;
	    case PERF_EVENT_DESTROY : /* delete PA event */
	      ret = ioctl(fd, IHK_OS_AUX_PERF_DESTROY, 0);
	      break;
	    default:
	      return(-EINVAL);
	  }
	  if (ret != 0) {
	    fprintf(stderr, "error: ihk_perfctl() \n");
	  }
	}
	return ret;
}

/* ihk_getperfevent */
int ihk_getperfevent (int index, unsigned long *counter, int n)
{
	char fn[128];
	int ret;
	int fd = 0;

	sprintf(fn, "/dev/mcos%d", index);
	fd = open(fn, O_RDONLY);
	if (fd < 0) {
	  ret=-ENOENT;
	}
	else {
	  ret = ioctl(fd, IHK_OS_AUX_PERF_GET, counter);
	  if (ret != 0) {
	    fprintf(stderr, "error: ihk_getperfevent() \n");
	  }
	}
	return ret;
}
