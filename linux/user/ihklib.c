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

//#define DEBUG_PRINT
#define MILLI_SEC 1000000

#ifdef DEBUG_PRINT
#define	dprintf(...) printf(__VA_ARGS__)
#define	eprintf(...) printf(__VA_ARGS__)
#else
#define dprintf(...) do { if (0) printf(__VA_ARGS__); } while (0)
#define	eprintf(...) printf(__VA_ARGS__)
#endif

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

/* ihk_getihk_info */
int ihk_getihk_info (ihk_info *info) 
{
	int fd = 0;
	char fn[128];
	int ret;
	DIR *dir;
	struct dirent *direp;
	char query_result[1024];
	unsigned long size = 0;
	int numa_node_number = 0;
	int ihk_mem_count = 0;
	int os_mem_count = 0;
	int os_count = 0;
	char *token;
	char *token1;

	int store_ihk_mem_chunks = 0;
	int store_os_mem_chunks = 0;
	ihk_mem_chunk *ihk_chunk_pos = NULL;
	ihk_mem_chunk *os_chunk_pos = NULL;

	/* get IHK memory info. */
	dprintf("ihk memory \n");
	if (info->reserved_mem_chunks != NULL) {
		store_ihk_mem_chunks = 1;	
		ihk_chunk_pos = info->reserved_mem_chunks;
	}
	memset(query_result, 0, sizeof(query_result));
	fd = open("/dev/mcd0", O_RDWR);
	if (fd < 0) {
	} else {
		ret = ioctl(fd, IHK_DEVICE_QUERY_MEM, query_result);
		if (ret != 0) {
			dprintf("error: querying IHK MEM\n");
		}
		if (ret == 0) {
			dprintf("query_result:%s\n",query_result);	
			token = strtok(query_result,delim);
			while (token != NULL) {
				ihk_mem_count++;
				size = atol(token);
				token = strtok(NULL,delim);
				if (token != NULL) {
					numa_node_number = atol(token);
				}
				token = strtok(NULL,delim);		
				if (store_ihk_mem_chunks == 1) {
					ihk_chunk_pos->size = size;
					ihk_chunk_pos->numa_node_number = numa_node_number;		
					dprintf("memchunk %lu @ %d node stored %p.\n", size, numa_node_number,ihk_chunk_pos);
					ihk_chunk_pos = ihk_chunk_pos + sizeof(ihk_mem_chunk);
				} 
				else {
					dprintf("memchunk %lu @ %d node not stored.\n", size, numa_node_number);
				}		 
			}
			dprintf("ihk_mem_count:%d\n", ihk_mem_count);
		}
		close(fd);
	}
	info->num_reserved_mem_chunks = ihk_mem_count;

	/* get IHK CPU info. */
	dprintf("ihk cpu \n");
	memset(query_result, 0, sizeof(query_result));
	fd = open("/dev/mcd0", O_RDWR);
	if (fd < 0) {
	} 
	else {
		ret = ioctl(fd, IHK_DEVICE_QUERY_CPU, query_result);
		if (ret != 0) {
				fprintf(stderr, "error: querying IHK CPU\n");
		}
		if (ret == 0) {
			dprintf("%s\n", query_result);
			CPU_ZERO(&cpu_set);
			token1 = strtok(query_result,delim1);
			while (token1 != NULL ) {
				set_cpu(token1);
				token1 = strtok(NULL,delim1);	
			}
			info->reserved_cpu_set = cpu_set;
		}
		close(fd);
	}

	/* get OS MEM info. */
	dprintf("os mem \n");
	if (info->assigned_mem_chunks != NULL ) {
		store_os_mem_chunks = 1;	
		os_chunk_pos = info ->assigned_mem_chunks;
	}
	memset(query_result, 0, sizeof(query_result));
	fd = open("/dev/mcos0", O_RDONLY);
	if (fd < 0) {
	}
	else {
		ret = ioctl(fd, IHK_OS_QUERY_MEM, query_result);
		if (ret != 0) {
			fprintf(stderr, "error: querying OS MEM\n");
		}
		if (ret == 0) {
			dprintf("query_result:%s\n",query_result);
			token = strtok(query_result,delim);
			while (token != NULL) {
				os_mem_count++;	
				size = atol(token);
				token = strtok(NULL,delim);
				if (token != NULL) {
					numa_node_number = atol(token);
				}
				token = strtok(NULL,delim);		
				if (store_os_mem_chunks == 1) {
					os_chunk_pos->size = size;
					os_chunk_pos->numa_node_number = numa_node_number;
					os_chunk_pos = os_chunk_pos + sizeof(ihk_mem_chunk);
					dprintf("memchunk %lu @ %d node stored.\n", size, numa_node_number);
				}
				else {
					dprintf("memchunk %lu @ %d node not stored.\n", size, numa_node_number);
				}
			}		 
			dprintf("os_mem_count:%d\n", os_mem_count);
		}
		close(fd);
	}
	info->num_assigned_mem_chunks = os_mem_count;
	
	/* get OS CPU info. */
	dprintf("os cpu \n");
	memset(query_result, 0, sizeof(query_result));
	sprintf(fn, "/dev/mcos%d", 0);
	fd = open(fn, O_RDONLY);
	if (fd < 0) {
		perror("open");
	} 
	else {
		ret = ioctl(fd, IHK_OS_QUERY_CPU, query_result);
		if (ret != 0) {
			fprintf(stderr, "error: querying OS CPU\n");
		}
		if (ret == 0) {
			dprintf("%s\n", query_result);
			CPU_ZERO(&cpu_set);
			token1 = strtok(query_result,delim1);
			while (token1 != NULL) {
				set_cpu(token1);
				token1 = strtok(NULL,delim1);	
			}
			info->assigned_cpu_set = cpu_set;
		}
		close(fd);
	} 

	/* get os count */
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

	info->num_os = os_count;

	return(0);
}

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

static void* check_status (void *p) {
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
	} 
	else {
		while(1) {
  			ret = ioctl(fd, IHK_OS_STATUS, query_result);
  			switch (ret) {
				case 0: /* IHK_OS_STATUS_NOT_BOOTED */
				case 1: /* IHK_OS_STATUS_BOOTING */
				case 2: /* IHK_OS_STATUS_BOOTED */
				case 3: /* IHK_OS_STATUS_READY */
				case 4: /* IHK_OS_STATUS_SHUTDOWN */
				case 5: /* IHK_OS_STATUS_STOPPED */
					break;
				case 6: /* IHK_OS_STATUS_FAILED */
				case 7: /* IHK_OS_HUNGUP */
					ioctl(fd, IHK_OS_EVENT_SIGNAL, efd);
					return (0);
					break;
				default:
					break;
			}
			nanosleep(&req, NULL);
		}
	}
	return(0);	
}

int ihk_geteventfd(int index, int type) {
	char fn[128];
	int fd = 0;
	int ret = 0;
	int efd;
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
			switch (type ) {
				case 0:
					// resource type 0: physical memory 
					efd = eventfd(0, 0);
					ret = ioctl(fd, IHK_OS_REGISTER_EVENT, efd);
					break;
				case 1:
					// resource type 1:  os status
					efd = eventfd(0, 0);
					cparam.efd = efd;
					ioctl(fd, IHK_OS_REGISTER_EVENT, efd);
					ret = pthread_create(&thread_id, NULL, check_status, (void *)&cparam);
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
	DIR *dir;
	struct dirent *dirp;
	int oscount = 0;
	char str_nodenum[5];
	if ((dir = opendir(PATH_DEV)) == NULL) {
		perror("opendir");
		exit(-1);
	}
	while (dir && (dirp = readdir(dir))) {
		if ((strncmp(dirp->d_name,"mcos",4) == 0)) {
			oscount++;
			if (oscount <= n) {
				strncpy(str_nodenum,dirp->d_name+4, strlen(dirp->d_name)-3);
				oslist[oscount-1] = atoi(str_nodenum);
				dprintf("count:%d item:%d %s\n", oscount, oslist[oscount-1], str_nodenum);
			} 
		}
	}
	closedir(dir);
	return (oscount);
}

/* execute ihkconfig */
int ihk_config (int mcd, int comm, ihkconfig *config) {
	enum ihk_command_type cmd_type;
	char fn[1024];
	int i;
	int pid;
	int status;
	struct stat file_stat;
	int ret = 0;
	char index_str[128];
	char ihkconfig_cmd[] = IHKCONFIG_CMD;
	char *args[10];
	int cmd_fd[2];
	char cmd_result[1024];
	int require_cmd_result = 0;
	char *token1;
	ihk_mem_chunk *ihk_chunk_pos = NULL;
	int ihk_mem_count = 0;
	unsigned long size = 0;
	int numa_node_number = 0;
	char cpu_list[2048];
	char cpu_str[4];
	char mem_list[2048];
	char mem_str[128];
	char *p;
	
	cmd_type = comm;
	args[0]= ihkconfig_cmd;

	sprintf(fn, "/dev/mcd%d", mcd);
	if (stat(fn,&file_stat) != 0) {
		dprintf("no ihk device /dev/mcd%d\n", mcd);
		return(-ENOENT);
	} 
	else {
		sprintf(fn,"%d",mcd);
		args[1] = fn;
	}

	/* prepare commmand arguments */		
	switch (cmd_type) {
		case IHK_CONFIG_CREATE:
			dprintf("IHK_CONFIG_CREATE called.\n");
			args[2] = "create";
			args[3] = NULL;   
			break;
		case IHK_CONFIG_DESTROY:
			dprintf("IHK_CONFIG_DESTROY called.\n");
			snprintf(index_str,sizeof(index_str), "%d", config->os_index);
			args[2] = "destroy";
			args[3] = index_str;
			args[4] = NULL;   
			break;
		case IHK_CONFIG_RESERVE:
			dprintf("IHK_CONFIG_RESERVE called.\n");
			args[2] = "reserve";
			if (config->resource_type == IHK_RESOURCE_CPU) {
				args[3] = "cpu";
				memset(cpu_list, 0, sizeof(cpu_list));
				for (i = 0; i <= __CPU_SETSIZE; i++) {
					if (CPU_ISSET(i,&(config->cpu_set))) {
						snprintf(cpu_str,sizeof(cpu_str), "%d,", i);
						strcat(cpu_list, cpu_str);
					}
				}
				p = strchr(cpu_list, '\0');
				if (p != NULL) {
					*(p-1) = '\0';
				}
				args[4] = cpu_list;
			} 
			else if (config->resource_type == IHK_RESOURCE_MEM) {
				args[3] = "mem";
				memset(mem_list, 0, sizeof(mem_list));
				ihk_chunk_pos = config->mem_chunks;	
				for (i = 1; i <= config->num_mem_chunks; i++) {
					snprintf(mem_str, sizeof(mem_str), "%lu@%d,",
						ihk_chunk_pos->size, ihk_chunk_pos->numa_node_number);
					strcat(mem_list, mem_str);
					ihk_chunk_pos = ihk_chunk_pos + sizeof(ihk_mem_chunk);
				}
				p = strchr(mem_list, '\0');
				if (p != NULL) {
					*(p-1) = '\0';
				}
				dprintf("mem_list:%s\n", mem_list);
				args[4] = mem_list;
			} else {
				dprintf("Invalid resource_type.\n");
				return(-EINVAL);
			}
			args[5] = NULL;	
			break;
		case IHK_CONFIG_RELEASE:
			dprintf("IHK_CONFIG_RELEASE called.\n");
			args[2] = "release";
			if (config->resource_type == IHK_RESOURCE_CPU) {
				args[3] = "cpu";
				memset(cpu_list, 0, sizeof(cpu_list));
				for (i = 0; i<= __CPU_SETSIZE; i++) {
					if (CPU_ISSET(i, &(config->cpu_set))) {
						snprintf(cpu_str, sizeof(cpu_str), "%d,", i);
						strcat(cpu_list, cpu_str);
					}
				}
				p = strchr(cpu_list, '\0');
				if (p != NULL) {
					*(p-1) = '\0';
				}
				args[4] = cpu_list;
			} 
			else if (config->resource_type == IHK_RESOURCE_MEM) {
				args[3] = "mem";
				memset(mem_list, 0, sizeof(mem_list));
				ihk_chunk_pos = config->mem_chunks;	
				for (i = 1; i <= config->num_mem_chunks; i++) {
					snprintf(mem_str, sizeof(mem_str), "%lu@%d,",
						ihk_chunk_pos->size, ihk_chunk_pos->numa_node_number);
					strcat(mem_list, mem_str);
					ihk_chunk_pos = ihk_chunk_pos + sizeof(ihk_mem_chunk);
				}
				p = strchr(mem_list, '\0');
				if (p != NULL) {
					*(p-1) = '\0';
				}
				dprintf("mem_list:%s\n", mem_list);
				args[4] = mem_list;
			} 
			else {
				dprintf("Invalid resource_type.\n");
				return(-EINVAL);
			}
			args[5] = NULL;	
			break;
		case IHK_CONFIG_QUERY:
			dprintf("IHK_CONFIG_QUERY called.\n");
			require_cmd_result = 1;
			args[1] = "0";
			args[2] = "query";
			args[4] = NULL;   
			if (config->resource_type == IHK_RESOURCE_CPU) {
				args[3] = "cpu";
				CPU_ZERO(&cpu_set);
			} 
			else if (config->resource_type == IHK_RESOURCE_MEM) {
				args[3] = "mem";
			} 
			else {
				dprintf("Invalid resource_type.\n");
				return(-EINVAL);
			}
			break;
		case IHK_CONFIG_RESERVE_NUMA:
			dprintf("IHK_CONFIG_RESERVE_NUMA called.\n");
			args[2] = "reserve";
			args[3] = "mem";
			memset(mem_list, 0, sizeof(mem_list));
			ihk_chunk_pos = config->mem_chunks;	
			for (i = 1; i <= config->num_mem_chunks; i++) {
				snprintf(mem_str, sizeof(mem_str), "%lu@%d,",
					ihk_chunk_pos->size, config->numa_node);
				strcat(mem_list, mem_str);
				ihk_chunk_pos = ihk_chunk_pos + sizeof(ihk_mem_chunk);
			}
			p = strchr(mem_list, '\0');
			if (p != NULL) {
				*(p-1) = '\0';
			}
			dprintf("mem_list:%s\n", mem_list);
			args[4] = mem_list;
			args[5] = NULL;	
			break;
		case IHK_CONFIG_RELEASE_NUMA:
			dprintf("IHK_CONFIG_RELEASE_NUMA called.\n");
			args[2] = "release";
			args[3] = "mem";
			memset(mem_list, 0, sizeof(mem_list));
			ihk_chunk_pos = config->mem_chunks;	
			for (i = 1; i <= config->num_mem_chunks; i++) {
				snprintf(mem_str, sizeof(mem_str), "%lu@%d,",
					ihk_chunk_pos->size, config->numa_node);
				strcat(mem_list, mem_str);
				ihk_chunk_pos = ihk_chunk_pos + sizeof(ihk_mem_chunk);
			}
			p = strchr(mem_list,'\0');
			if (p != NULL) {
				*(p-1) = '\0';
			}
			dprintf("mem_list:%s\n", mem_list);
			args[4] = mem_list;
			args[5] = NULL;	
			break;
		default:
			dprintf("cmdtype:%d is unknown.\n", cmd_type);
			return(-EINVAL);
	}

	/* execute ihkconfig command */
	pipe(cmd_fd);
	pid	= fork ();
	if (pid == -1) {
		exit(-EINVAL);
	} 
	else if (pid == 0) {
		close(STDOUT_FILENO);
		close(STDERR_FILENO);
		dup2(cmd_fd[1], STDOUT_FILENO);
		close(cmd_fd[0]);
		execv(ihkconfig_cmd, args);
		_exit(0);
	} 
	else {
		if (require_cmd_result == 1) {
			memset(cmd_result, 0, sizeof(cmd_result));
			read(cmd_fd[0], cmd_result, sizeof(cmd_result)-1);
		}
		if (cmd_type != IHK_CONFIG_CREATE) { /* CREATE does not wait command. */
			waitpid(pid, &status, WUNTRACED);
			dprintf("parent(%d) waits child(%d).\n", getpid(), pid);
			if (WIFEXITED(status)) {
				dprintf(" child exits at %d\n", WEXITSTATUS(status));
				ret = WEXITSTATUS(status);
			}
		}
	}

	/* process results */	
	switch (cmd_type) {
		case IHK_CONFIG_CREATE:
			return(0); /* always returns 0 */
			break;
		case IHK_CONFIG_DESTROY:
			return(0); /* always returns 0 */
			break;
		case IHK_CONFIG_RESERVE:
		case IHK_CONFIG_RESERVE_NUMA:
		case IHK_CONFIG_RELEASE:
		case IHK_CONFIG_RELEASE_NUMA:
			if (ret != 0) {
				return(-EINVAL);
			}
			break;
		case IHK_CONFIG_QUERY:
			if (ret != 0) {
				return(-EINVAL);
			}
			cmd_result[strlen(cmd_result)-1] = '\0'; /* trim \n */
			dprintf("result:%s:", cmd_result);
			if (config->resource_type == IHK_RESOURCE_CPU) {
				CPU_ZERO(&cpu_set);
				token1 = strtok(cmd_result, delim1);
				while (token1 != NULL) {
					set_cpu(token1);
					token1 = strtok(NULL, delim1);
				}
				config->cpu_set = cpu_set;
				return(0);		
			} 
			else if (config->resource_type == IHK_RESOURCE_MEM) {
				ihk_chunk_pos = config->mem_chunks;
				token1 = strtok(cmd_result, delim);
				while (token1 != NULL) {
					size = atol(token1);
					token1 = strtok(NULL, delim);
					if (token1 != NULL) {
						numa_node_number = atol(token1);
					}
					if (config->mem_chunks !=NULL) {
						ihk_chunk_pos->size  = size;
						ihk_chunk_pos->numa_node_number = numa_node_number;
						ihk_chunk_pos = ihk_chunk_pos + sizeof(ihk_mem_chunk);
					}
					ihk_mem_count++;
					dprintf("ihk_mem_count:%d size:%lu no:%d\n", ihk_mem_count, size, numa_node_number);
					token1 = strtok(NULL, delim);
				}
				
				config->num_mem_chunks = ihk_mem_count;	
				return(0);		
			} 
			else {
				dprintf("Invalid resource_type.\n");
				return(-EINVAL);
			}
			break;
		default:
			dprintf("cmdtype:%d is unknown.\n", cmd_type);
			return(-EINVAL);
	}
	return(0);
}

static int do_boot(int fd)
{
	int r = ioctl(fd, IHK_OS_BOOT, 0);
	if (r != 0) {
		fprintf(stderr, "error: booting\n");
	}
	dprintf("ret = %d\n", r);
	return r;
}

static int do_load(int fd)
{
	char *fn;
	if (__argc > 3) {
		fn = __argv[3];
	} 
	else {
		return(-1);
	}
	int r = ioctl(fd, IHK_OS_LOAD, (unsigned long)fn);

	if (r != 0) {
		fprintf(stderr, "error: loading %s\n", fn);
	}
	dprintf("ret = %d\n", r);
	return r;
}

static int do_shutdown(int fd)
{
	int r = ioctl(fd, IHK_OS_SHUTDOWN, 0);
	dprintf("ret = %d\n", r);
	if (r != 0) {
		fprintf(stderr, "error: shutting down\n");
	}
	return r;
}

static int do_assign(int fd)
{
	int ret;

	if (__argc < 5) {
		return -1;
	}

	if (!strcmp(__argv[3], "cpu")) {
		ret = ioctl(fd, IHK_OS_ASSIGN_CPU, __argv[4]);

		if (ret != 0) {
			fprintf(stderr, "error: assigning CPUs: %s\n", __argv[4]);
		}
	}
	else if (!strcmp(__argv[3], "mem")) {
		ret = ioctl(fd, IHK_OS_ASSIGN_MEM, __argv[4]);

		if (ret != 0) {
			fprintf(stderr, "error: assigning memory: %s\n", __argv[4]);
		}
	}
	else {
		ret = -EINVAL;
	}

	dprintf("ret = %d\n", ret);
	return ret;
}

static int do_release(int fd)
{
	int ret;

	if (__argc < 4) {
		return -1;
	}

	if (!strcmp(__argv[3], "cpu")) {
		ret = ioctl(fd, IHK_OS_RELEASE_CPU, __argv[4]);

		if (ret != 0) {
			fprintf(stderr, "error: releasing CPUs: %s\n", __argv[4]);
		}
	}
	else if (!strcmp(__argv[3], "mem")) {
		ret = ioctl(fd, IHK_OS_RELEASE_MEM, __argv[4]);

		if (ret != 0) {
			fprintf(stderr, "error: releasing memory: %s\n", __argv[4]);
		}
	}
	else {
		ret = -EINVAL;
	}

	dprintf("ret = %d\n", ret);
	return ret;
}

static int do_query(int fd)
{
	int ret; 
	char query_result[8192];

	if (__argc < 3) {
		int r = ioctl(fd, IHK_OS_QUERY_STATUS);
		if (r != 0) {
			fprintf(stderr, "error: querying\n");
		}
		dprintf("status = %d\n", r);

		return r;
	}
	
	memset(query_result, 0, sizeof(query_result));

	if (!strcmp(__argv[3], "cpu")) {
		ret = ioctl(fd, IHK_OS_QUERY_CPU, query_result);

		if (ret != 0) {
			fprintf(stderr, "error: querying CPUs\n");
		}
	}
	else if (!strcmp(__argv[3], "mem")) {
		ret = ioctl(fd, IHK_OS_QUERY_MEM, query_result);

		if (ret != 0) {
			fprintf(stderr, "error: querying memory\n");
		}
	}
	else {
		ret = -EINVAL;
	}

	if (ret == 0) {
		printf("%s\n", query_result);
	}

	dprintf("ret = %d\n", ret);
	return ret;
}

static int do_query_free_mem(int fd)
{
	int r = ioctl(fd, IHK_OS_QUERY_FREE_MEM);
	
	if (r != 0) {
		fprintf(stderr, "error: querying free memory\n");
	}

	printf("number of free pages (4kB): %d\n", r);
	return r;
}

static int do_kargs(int fd)
{
	int r;

	if (__argc <= 3) {
		fprintf(stderr, "error: no arg specified.\n");
		return 1;
	} 

	r = ioctl(fd, IHK_OS_SET_KARGS, (char *)__argv[3]);
	if (r != 0) {
		fprintf(stderr, "error: sending IRQ\n");
	}
	dprintf("ret = %d\n", r);
	return r;
}

static int do_kmsg(int fd)
{
	char buf[16384];
	int r = ioctl(fd, IHK_OS_READ_KMSG, (unsigned long)buf);
	if (r >= 0) {
		buf[r] = 0;
		printf("read kmsg %s\n", buf);
		return 0;
	}
	else {
		fprintf(stderr, "error querying kmsg\n");
		return 1;
	}
}

static int do_clear_kmsg(int fd)
{
	int r = ioctl(fd, IHK_OS_CLEAR_KMSG, 0);

	dprintf("ret = %d\n", r >= 0 ? r : -errno);
	return r >= 0 ? r : -errno;
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

    char *vmfile;
    int opt = 1; /* 0:DUMP_ALL_MEM 1:DUMP_CHUNK_MEM */

	mem_chunks = malloc(PHYS_CHUNKS_DESC_SIZE);
	if (!mem_chunks) {
		perror("allocating mem_chunks buffer: ");
		return 1;
	}

    if (__argc == 6) {
        opt = atoi(__argv[4]);
        vmfile = __argv[5];
        dprintf("opt:%d vmfile:%s\n",opt,vmfile);
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

/* execute ihkosctl commamd */
int ihk_osctl(int index, int comm, ihkosctl *ctl) {
	enum ihk_osctl_command_type cmd_type;
	char fn[1024];
	char os_fn[1024];
	int i;
	int pid;
	int status;
	struct stat file_stat;
	int ret = 0;
	char ihkosctl_cmd[] = IHKOSCTL_CMD;
	char *args[10];
	int cmd_fd[2];
	char cmd_result[1024];
	int require_cmd_result = 0;
	char *token1;
	ihk_mem_chunk *ihk_chunk_pos = NULL;
	int ihk_mem_count = 0;
	unsigned long size = 0;
	int numa_node_number = 0;
	char cpu_list[2048];
	char cpu_str[4];
	char mem_list[2048];
	char mem_str[128];
	char *p;

	cmd_type = comm;
	args[0] = ihkosctl_cmd;
	__argv = args;

	sprintf(fn, "/dev/mcos%d", index);
	sprintf(os_fn, "/dev/mcos%d", index);
	if (stat(fn, &file_stat) != 0) {
		dprintf("no os instance /dev/mcos%d.\n", index);
		return(-ENOENT);
	}
	else {
		sprintf(fn, "%d", index);
		args[1] = fn;
	}
		
	/* prepare command arguments */
	pipe(cmd_fd);
	pid = fork();
	if (pid == 0) {
		int r = 0;
		int fd;

		fd = open(os_fn, O_RDONLY);
		if (fd == -1) {
			perror("open");
			exit(255);
		}
		close(STDOUT_FILENO);
		close(STDERR_FILENO);
		dup2(cmd_fd[1], STDOUT_FILENO);
		close(cmd_fd[0]);
		switch (cmd_type) {
		    case IHK_OSCTL_LOAD:
				dprintf("IHK_OSCTL_LOAD called.\n");
				args[2] = "load";
				if (ctl->image == NULL) {
					dprintf("ihk_osctl_load: no image.\n");
					exit(255);
				}
				args[3] = ctl->image;
				args[4] = NULL;
				__argc = 4;
				r = do_load(fd);
				break;
		    case IHK_OSCTL_BOOT:
				dprintf("IHK_OSCTL_BOOT called.\n");
				args[2] = "boot";
				args[3] = NULL;
				__argc = 3;
				r = do_boot(fd);
				break;
		    case IHK_OSCTL_SHUTDOWN:
				dprintf("IHK_OSCTL_SHUTDOWN called.\n");
				args[2] = "shutdown";
				args[3] = NULL;
				__argc = 3;
				r = do_shutdown(fd);
				break;
		    case IHK_OSCTL_ASSIGN:
				dprintf("IHK_OSCTL_ASSIGN called.\n");
				args[2] = "assign";
				if (ctl->resource_type == IHK_RESOURCE_CPU) {
					args[3] = "cpu";
					memset(cpu_list, 0, sizeof(cpu_list));
					for (i = 0; i<= __CPU_SETSIZE; i++) {
						if (CPU_ISSET(i, &(ctl->cpu_set))) {
							snprintf(cpu_str,sizeof(cpu_str),"%d,",i);
							strcat(cpu_list,cpu_str);
						}
					}
					p = strchr(cpu_list,'\0');
					if (p != NULL) {
						*(p-1) = '\0';
					}
					args[4] = cpu_list;
				} 
				else if (ctl->resource_type == IHK_RESOURCE_MEM) {
					args[3] = "mem";
					memset(mem_list, 0, sizeof(mem_list));
					ihk_chunk_pos = ctl->mem_chunks;	
					for (i = 1; i <= ctl->num_mem_chunks; i++) {
						snprintf(mem_str, sizeof(mem_str), "%lu@%d,",
							ihk_chunk_pos->size, ihk_chunk_pos->numa_node_number);
						strcat(mem_list, mem_str);
						ihk_chunk_pos = ihk_chunk_pos + sizeof(ihk_mem_chunk);
					}
					p = strchr(mem_list, '\0');
					if (p != NULL) {
						*(p-1) = '\0';
					}
					dprintf("mem_list:%s\n",mem_list);
					args[4] = mem_list;
				} 
				else {
					dprintf("Invalid resource type.\n");
					exit(255);
				}
				args[5] = NULL;	
				__argc = 5;
				r = do_assign(fd);
				break;
		    case IHK_OSCTL_RELEASE:
				dprintf("IHK_OSCTL_RELEASE called.\n");
				args[2] = "release";
				if (ctl->resource_type == IHK_RESOURCE_CPU) {
					args[3] = "cpu";
					memset(cpu_list, 0, sizeof(cpu_list));
					for (i = 0; i<= __CPU_SETSIZE; i++) {
						if (CPU_ISSET(i, &(ctl->cpu_set))) {
							snprintf(cpu_str, sizeof(cpu_str), "%d,", i);
							strcat(cpu_list,cpu_str);
						}
					}
					p = strchr(cpu_list, '\0');
					if (p != NULL) {
						*(p-1) = '\0';
					}
					args[4] = cpu_list;
				} 
				else if (ctl->resource_type == IHK_RESOURCE_MEM) {
					args[3] = "mem";
					memset(mem_list, 0, sizeof(mem_list));
					ihk_chunk_pos = ctl->mem_chunks;	
					for (i = 1; i <= ctl->num_mem_chunks; i++) {
						snprintf(mem_str, sizeof(mem_str), "%lu@%d,",
							ihk_chunk_pos->size, ihk_chunk_pos->numa_node_number);
						strcat(mem_list,mem_str);
						ihk_chunk_pos = ihk_chunk_pos + sizeof(ihk_mem_chunk);
					}
					p = strchr(mem_list, '\0');
					if (p != NULL) {
						*(p-1) = '\0';
					}
					dprintf("mem_list:%s\n", mem_list);
					args[4] = mem_list;
				} 
				else {
					dprintf("Invalid resource_type.\n");
					exit(255);
				}
				args[5] = NULL;	
				__argc = 5;
				r = do_release(fd);
				break;
		    case IHK_OSCTL_QUERY:
				dprintf("IHK_OSCTL_QUERY called.\n");
				args[2] = "query";
				require_cmd_result = 1;
				if (ctl->resource_type == IHK_RESOURCE_CPU) {
					args[3] = "cpu";
				} 
				else if (ctl->resource_type == IHK_RESOURCE_MEM) {
					args[3] = "mem";
				} 
				else {
					dprintf("Invalid resource_type.\n");
					exit(255);
				}
				args[4] = NULL;	
				__argc = 4;
				r = do_query(fd);
				break;
		    case IHK_OSCTL_QUERY_FREE_MEM:
				dprintf("IHK_OSCTL_QUERY_FREE_MEM called.\n");
				args[2] = "query_free_mem";
				require_cmd_result = 1;
				args[3] = NULL;	
				__argc = 3;
				r = do_query_free_mem(fd);
				break;
		    case IHK_OSCTL_KARGS:
				dprintf("IHK_OSCTL_KARGS called.\n");
				args[2] = "kargs";
				args[3] = ctl->kargs;
				args[4] = NULL;	
				__argc = 4;
				r = do_kargs(fd);
				break;
		    case IHK_OSCTL_KMSG:
				dprintf("IHK_OSCTL_KMSG called.\n");
				require_cmd_result = 1;
				args[2] = "kmsg";
				args[3] =  NULL;
				__argc = 3;
				r = do_kmsg(fd);
				break;
		    case IHK_OSCTL_CLEAR_KMSG:
				dprintf("IHK_OSCTL_CLEAR_KMSG called.\n");
				args[2] = "clear_kmsg";
				args[3] = NULL;	
				__argc = 3;
				r = do_clear_kmsg(fd);
				break;
		    case IHK_OSCTL_ASSIGN_NUMA:
				dprintf("IHK_OSCTL_ASSIGN_NUMA called.\n");
				args[2] = "assign";
				args[3] = "mem";
				memset(mem_list, 0, sizeof(mem_list));
				ihk_chunk_pos = ctl->mem_chunks;
				for (i = 1; i <= ctl->num_mem_chunks; i++) {
					snprintf(mem_str, sizeof(mem_str), "%lu@%d,",
						ihk_chunk_pos->size, ctl->numa_node);
					strcat(mem_list, mem_str);
				ihk_chunk_pos = ihk_chunk_pos + sizeof(ihk_mem_chunk);
				}
				p = strchr(mem_list, '\0');
				if (p != NULL) {
					*(p-1) = '\0';
				}
				dprintf("mem_list:%s\n", mem_list);
				args[4] = mem_list;
				args[5] = NULL;	
				__argc = 5;
				r = do_assign(fd);
				break;
		    case IHK_OSCTL_RELEASE_NUMA:
				dprintf("IHK_OSCTL_RELEASE_NUMA called.\n");
				args[2] = "release";
				args[3] = "mem";
				memset(mem_list, 0, sizeof(mem_list));
				ihk_chunk_pos = ctl->mem_chunks;
				for (i = 1; i <= ctl->num_mem_chunks; i++) {
					snprintf(mem_str, sizeof(mem_str), "%lu@%d,",
						ihk_chunk_pos->size, ctl->numa_node);
					strcat(mem_list, mem_str);
					ihk_chunk_pos = ihk_chunk_pos + sizeof(ihk_mem_chunk);
				}
				p = strchr(mem_list, '\0');
				if (p != NULL) {
					*(p-1) = '\0';
				}
				dprintf("mem_list:%s\n", mem_list);
				args[4] = mem_list;
				args[5] = NULL;
				__argc = 5;
				r = do_release(fd);
				break;
		    default:
				dprintf("ihkosctl cmdtype:%d is unknown.\n", cmd_type);
				exit(255);
		}
		exit(r);
	}
	else if (pid == -1) {
		/* execute ihkosctl command */
		return -errno;
	}

	if (cmd_type == IHK_OSCTL_KMSG) {	
		read(cmd_fd[0], ctl->kmsg, ctl->kmsg_size);
	} 
	else if (require_cmd_result == 1) {
		memset(cmd_result, 0, sizeof(cmd_result));
		read(cmd_fd[0], cmd_result, sizeof(cmd_result)-1);
	}
	if (cmd_type != IHK_OSCTL_SHUTDOWN) { /* shutdown does not wait command. */
		waitpid(pid, &status, WUNTRACED);
		dprintf("parent(%d) waits child(%d).\n", getpid(), pid);
		if (WIFEXITED(status)) {
			dprintf(" child exits at %d\n", WEXITSTATUS(status));
			ret = WEXITSTATUS(status);
			if(ret == 255)
				return -EINVAL;
		}
	}

	/* process results */
	switch (cmd_type) {
	    case IHK_OSCTL_LOAD:
	    case IHK_OSCTL_BOOT:
	    case IHK_OSCTL_ASSIGN:
	    case IHK_OSCTL_RELEASE:
	    case IHK_OSCTL_KARGS:
	    case IHK_OSCTL_CLEAR_KMSG:
	    case IHK_OSCTL_ASSIGN_NUMA:
	    case IHK_OSCTL_RELEASE_NUMA:
			if (ret != 0) {
				return -EINVAL;
			}
			break;
	    case IHK_OSCTL_SHUTDOWN:
			return 0; /* shutdown always returns 0. */
			break;
	    case IHK_OSCTL_QUERY:
			if (ret != 0) {
				return -EINVAL;
			}
			cmd_result[strlen(cmd_result)-1] = '\0'; /* trim \n */
			dprintf("result:%s:", cmd_result);
			if (ctl->resource_type == IHK_RESOURCE_CPU) {
				CPU_ZERO(&cpu_set);
				token1 = strtok(cmd_result, delim1);
				while (token1 != NULL) {
					set_cpu(token1);
					token1 = strtok(NULL, delim1);
				}
				ctl->cpu_set = cpu_set;
			} 
			else if (ctl->resource_type == IHK_RESOURCE_MEM) {
				ihk_chunk_pos = ctl->mem_chunks;
				token1 = strtok(cmd_result, delim);
				while (token1 != NULL) {
					size = atol(token1);
					token1 = strtok(NULL, delim);
					if (token1 != NULL) {
						numa_node_number = atol(token1);
					}
					ihk_chunk_pos->size = size;
					ihk_chunk_pos->numa_node_number = numa_node_number;
					ihk_chunk_pos = ihk_chunk_pos + sizeof(ihk_mem_chunk);
					ihk_mem_count++;
					dprintf("ihk_mem_count:%d size:%lu no:%d\n", ihk_mem_count, size, numa_node_number);
					token1 = strtok(NULL, delim);
				}
				ctl->num_mem_chunks = ihk_mem_count;	
			} 
			else {
				dprintf("Invalid resource_type.\n");
				return -EINVAL;
			}
			break;
	    case IHK_OSCTL_QUERY_FREE_MEM:
			if (ret != 0) {
				return -EINVAL;
			}
			ihk_chunk_pos = ctl->mem_chunks;
			token1 = strtok(cmd_result, delim);
			while (token1 != NULL) {
				size = atol(token1);
				token1 = strtok(NULL, delim);
				if (token1 != NULL) {
					numa_node_number = atol(token1);
				}
				ihk_chunk_pos->size = size;
				ihk_chunk_pos->numa_node_number = numa_node_number;
				ihk_chunk_pos = ihk_chunk_pos + sizeof(ihk_mem_chunk);
				ihk_mem_count++;
				dprintf("ihk_mem_count:%d size:%lu no:%d\n", ihk_mem_count, size, numa_node_number);
				token1 = strtok(NULL, delim);
			}
			ctl->num_mem_chunks = ihk_mem_count;	
			break;
	    default:
			dprintf("cmdtype:%d is unknown.\n", cmd_type);
			return -EINVAL;
		}
		return(0);
}

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

/* ihk_getrusage */
int ihk_getrusage(int index, ihk_rusage *rusage) {
	unsigned long val;
	int ret;
	int numa_count;
	int i;
	FILE *fp;
	int BUF_SIZE = 2048;
	char buf[BUF_SIZE];
	char key[8];
	char *p;
	char path[1024] = {'\0'};
	
	sprintf(path, "/dev/mcos%d", index);	
	fp = fopen(path, "r");
	if (fp == NULL) {
		return(-ENOENT);
	} 
	else {
		fclose(fp);
	}

	/* read rss */
	snprintf(path, 1024, PATH_SYS_RSS, index);
	if ((ret = read_sysfs_key_val(path, KEY_SYS_RSS, &val)) == 0) {
		rusage->rss = val;
	} 
	else {
		dprintf("read rss failed.\n");
	}

	/* read cache */
	if ((ret = read_sysfs_key_val(PATH_SYS_CACHE, KEY_SYS_CACHE, &val)) == 0) {
		rusage->cache = val;
	} 
	else {
		dprintf("read cache failed.\n");
	}

	/* read rss_huge */
	snprintf(path, 1024, PATH_SYS_RSS_HUGE, index);
	if ((ret = read_sysfs_key_val(path, KEY_SYS_RSS_HUGE, &val)) == 0) {
		rusage->rss_huge = val;
	} 
	else {
		dprintf("read rss_huge failed. %s\n", path);
	}

	/* read mapped_file */
	if ((ret = read_sysfs_key_val(PATH_SYS_MAPPED_FILE, KEY_SYS_MAPPED_FILE, &val)) == 0) {
		rusage->mapped_file = val;
	} 
	else {
		dprintf("read mapped_file failed.\n");
	}

	/* read max_usage */
	snprintf(path, 1024, PATH_SYS_MAX_USAGE, index);
	if ((ret = read_sysfs_simple_val(path, &val)) == 0) {
		rusage->max_usage = val;
	} 
	else {
		dprintf("read max_usage failed.\n");
	}

	/* read kmem_usage */
	snprintf(path, 1024, PATH_SYS_KMEM_USAGE, index);
	if ((ret = read_sysfs_simple_val(path, &val)) == 0) {
		rusage->kmem_usage = val;
	} 
	else {
		dprintf("read kmem_usage failed.\n");
	}
	
	/* read kmax_usage */
	snprintf(path, 1024, PATH_SYS_KMAX_USAGE, index);
	if ((ret = read_sysfs_simple_val(path, &val)) == 0) {
		rusage->kmax_usage = val;
	} 
	else {
		dprintf("read kmax_usage failed.\n");
	}

	/* read num_numa_nodes */
	snprintf(path, 1024, PATH_SYS_NUM_NUMA_NODES, index);
	if ((ret = read_sysfs_simple_val(path, &val)) == 0) {
		rusage->num_numa_nodes = val;
	} 
	else {
		dprintf("read num_numa_nodes failed.\n");
	}
	numa_count = rusage->num_numa_nodes;

	/* read numa_stat */
	snprintf(path, 1024, PATH_SYS_NUMA_STAT, index);
	if ((fp = fopen(path, "r")) == NULL) {
		perror("fopen");
	} 
	else {
		memset(buf, 0, BUF_SIZE);	
		while (fgets(buf, BUF_SIZE, fp) != NULL) {
			if (strncmp(buf, KEY_SYS_NUMA_STAT, strlen(KEY_SYS_NUMA_STAT)) == 0) {
				p = strtok(buf, " =");  /* get label "total" */
				p = strtok(NULL, " ="); /* get total value */
				for (i = 0; i < numa_count; i++) {
					sprintf(key, "N%d=", i);
					p = strtok(NULL, " ="); /* get label "N0" */
					p = strtok(NULL, " ="); /* get value */
					if (p != NULL) {
						dprintf(" char:%s.\n", p);
						*(rusage->numa_stat+sizeof(unsigned long)*i) = strtoul(p, NULL, 0);
						dprintf(" value:%lu.\n", *(rusage->numa_stat+sizeof(unsigned long)*i));
					}
				}
			}		
			memset(buf, 0, BUF_SIZE);	
		}
		fclose(fp); 
	}	

	/* read hugetlb */
	snprintf(path, 1024, PATH_SYS_HUGETLB, index);
	if ((ret = read_sysfs_simple_val(path, &val)) == 0) {
		rusage->hugetlb = val;
	} 
	else {
		dprintf("read hugetlb failed.\n");
	}

	/* read hugetlb_max */
	snprintf(path, 1024, PATH_SYS_HUGETLB_MAX, index);
	if ((ret = read_sysfs_simple_val(path, &val)) == 0) {
		rusage->hugetlb_max = val;
	} 
	else {
		dprintf("read hugetlb_max failed.\n");
	}

	/* read stat_system */
	snprintf(path, 1024, PATH_SYS_STAT_SYSTEM, index);
	if ((ret = read_sysfs_key_val(path, KEY_SYS_STAT_SYSTEM, &val)) == 0) {
		rusage->stat_system = val;
	} 
	else {
		dprintf("read stat_system failed\n");
	}

	/* read stat_user */
	snprintf(path, 1024, PATH_SYS_STAT_USER, index);
	if ((ret = read_sysfs_key_val(path, KEY_SYS_STAT_USER, &val)) == 0) {
		rusage->stat_user = val;
	} 
	else {
		dprintf("read stat_user failed\n");
	}

	/* read usage */
	snprintf(path, 1024, PATH_SYS_USAGE, index);
	if ((ret = read_sysfs_simple_val(path, &val)) == 0) {
		rusage->usage = val;
	} 
	else {
		dprintf("read usage failed.\n");
	}
	
	/* read usage_per_cpu */
	snprintf(path, 1024, PATH_SYS_USAGE_PER_CPU, index);
	if ((fp = fopen(path,"r")) == NULL) {
		perror("fopen");
	} 
	else {
		memset(buf, 0, BUF_SIZE);	
		fgets(buf, BUF_SIZE, fp);	
		p = strtok(buf, " ");
		for (i = 0; i < sizeof(cpu_set_t)/8; i++) {
			if (p == NULL) { break;}
			rusage->usage_per_cpu[i] = strtoul(p, NULL, 0);
			p = strtok(NULL, " ");
		}	
		fclose(fp); 
	}

	/* read num_threads */
	snprintf(path, 1024, PATH_SYS_NUM_THREADS, index);
	if ((ret = read_sysfs_simple_val(path, &val)) == 0) {
		rusage->num_threads = val;
	} 
	else {
		dprintf("read num_threads failed.\n");
	}
	
	/* read max_num_threads */
	snprintf(path, 1024, PATH_SYS_MAX_NUM_THREADS, index);
	if ((ret = read_sysfs_simple_val(path, &val)) == 0) {
		rusage->max_num_threads = val;
	} 
	else {
		dprintf("read max_num_threads failed.\n");
	}
	return 0;
}

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
	
/* ihk_getosinfo */
int ihk_getosinfo (int index, ihk_osinfo *osinfo) {
	int fd = 0;
	char fn[128];
	int ret;	
	char query_result[1024];
	unsigned long size = 0;
	int numa_node_number = 0;
	int os_mem_count = 0;
	char *token;
	char *token1;

	int store_os_mem_chunks = 0;
	ihk_mem_chunk *os_chunk_pos = NULL;

	sprintf(fn, "/dev/mcos%d", index);
	fd = open(fn, O_RDWR);
	if (fd < 0) {
		return (-ENOENT);
	}
	close(fd);

	osinfo->status = 0;
	
	/* get OS MEM info. */
	dprintf("os mem \n");
	if (osinfo->mem_chunks != NULL) {
		store_os_mem_chunks = 1;
		os_chunk_pos = osinfo ->mem_chunks;
	}
	memset(query_result, 0, sizeof(query_result));
	sprintf(fn, "/dev/mcos%d", index);
	fd = open(fn, O_RDONLY);
	if (fd < 0) {
	} 
	else {
		ret = ioctl(fd, IHK_OS_QUERY_MEM, query_result);
		if (ret != 0) {
			fprintf(stderr, "error: querying OS MEM\n");
		}
		if (ret == 0) {
			dprintf("query_result:%s\n", query_result);
			token = strtok(query_result, delim);
			while ( token != NULL ) {	
				os_mem_count++;
				dprintf("	token1:%s\n", token);
				size = atol(token);
				token = strtok(NULL, delim);
				dprintf("	token2:%s\n", token);
				if (token != NULL) {
					numa_node_number = atol(token);
				}
				if (store_os_mem_chunks == 1) {
					os_chunk_pos->size = size;
					os_chunk_pos->numa_node_number = numa_node_number;
					os_chunk_pos = os_chunk_pos + sizeof(ihk_mem_chunk);
					dprintf("store %lu @ %d node\n", size, numa_node_number);
				}
				token = strtok(NULL, delim);
			}
			dprintf("os_mem_count:%d\n", os_mem_count);
		}
		close(fd);
	}
	osinfo->num_mem_chunks = os_mem_count;

   /* get OS CPU info. */
	dprintf("os cpu \n");
	memset(query_result, 0, sizeof(query_result));
	sprintf(fn, "/dev/mcos%d", index);
	fd = open(fn, O_RDONLY);
	if (fd < 0) {
		perror("open");
	} 
	else {
		ret = ioctl(fd, IHK_OS_QUERY_CPU, query_result);
		if (ret != 0) {
			fprintf(stderr, "error: querying OS CPU\n");
		}
		if (ret == 0) {
			dprintf("%s\n", query_result);
			CPU_ZERO(&cpu_set);
			token1 = strtok(query_result, delim1);
			while (token1 != NULL) {
				set_cpu(token1);
				token1 = strtok(NULL, delim1);
			}
			osinfo->mask = cpu_set;
		}
	}
	
	/* get os status */
	dprintf("os status \n");
	memset(query_result, 0, sizeof(query_result));
	sprintf(fn, "/dev/mcos%d", 0);
	fd = open(fn, O_RDONLY);
	if (fd < 0) {
		perror("open");
	}
	else {
		ret = ioctl(fd, IHK_OS_STATUS, query_result);
		switch (ret) {
		    case 0: /* IHK_OS_STATUS_NOT_BOOTED */
				osinfo->status = IHK_STATUS_INACTIVE;
				break;
		    case 1: /* IHK_OS_STATUS_BOOTING */
		    case 2: /* IHK_OS_STATUS_BOOTED */
				osinfo->status = IHK_STATUS_BOOTING;
				break;
		    case 3: /* IHK_OS_STATUS_READY */
				osinfo->status = IHK_STATUS_RUNNING;
				break;
		    case 4: /* IHK_OS_STATUS_SHUTDOWN */
		    case 5: /* IHK_OS_STATUS_STOPPED */
				osinfo->status = IHK_STATUS_SHUTDOWN;
				break;
		    case 6: /* IHK_OS_STATUS_FAILED */
				osinfo->status = IHK_STATUS_PANIC;
				break;
		    case 7: /* IHK_OS_HUNGUP */
				osinfo->status = IHK_STATUS_HUNGUP;
				break;
		    case 8: /* IHK_OS_FREEZING */
				osinfo->status = IHK_STATUS_FREEZING;
				break;
		    case 9: /* IHK_OS_FROZEN */
				osinfo->status = IHK_STATUS_FROZEN;
				break;
		    default:
				osinfo->status = IHK_STATUS_INACTIVE;
				break;
		}
	}	
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
int ihk_freeze (int index)
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
	  ret = ioctl(fd, IHK_OS_FREEZE, 0);
      if (ret != 0) {
        fprintf(stderr, "error: ihk_freeze() \n");
      }
    }
	return ret;
}

/* ihk_thaw */
int ihk_thaw (int index)
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
	  ret = ioctl(fd, IHK_OS_THAW, 0);
	  if (ret != 0) {
	    fprintf(stderr, "error: ihk_thaw() \n");
	  }
	}
	return ret;
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
