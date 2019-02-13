/**
 * \file ihklib.c
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
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
#include <linux/limits.h>
#include <sched.h>
#include <linux/version.h>
#include <linux/sched.h>
#include <config.h>
#include <ihk/ihk_host_user.h>
#include <ihk/ihklib.h>
#include <ihk/ihklib_private.h>
#include <ihk/ihk_arch_rusage.h>

int __argc;
char **__argv;

int loglevel = IHKLIB_LOGLEVEL_ERR;

//#define DEBUG

#ifdef DEBUG
#define dprintf(fmt, args...) do {	\
	printf(fmt, ##args);		\
} while (0)
#else
#define dprintf(fmt, args...) do {  } while (0)
#endif

#define eprintf(fmt, args...) do {		\
	if (loglevel >= IHKLIB_LOGLEVEL_ERR) {	\
		fprintf(stderr, fmt, ##args);	\
	}					\
} while (0)

#define PHYSMEM_NAME_SIZE 32

#define CHKANDJUMP(cond, err, fmt, args...) do {	\
	if (cond) {					\
		eprintf(fmt, ##args);			\
		ret = err;				\
		goto out;				\
	}						\
} while(0)


struct namespace_file namespace_files[] = {
	{ .nstype = CLONE_NEWUSER,	.name = "ns/user", .fd = -1 },
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,6,0)
	{ .nstype = CLONE_NEWCGROUP,	.name = "ns/cgroup", .fd = -1 },
#endif
	{ .nstype = CLONE_NEWIPC,	.name = "ns/ipc", .fd = -1 },
	{ .nstype = CLONE_NEWUTS,	.name = "ns/uts", .fd = -1 },
	{ .nstype = CLONE_NEWNET,	.name = "ns/net", .fd = -1 },
	{ .nstype = CLONE_NEWPID,	.name = "ns/pid", .fd = -1 },
	{ .nstype = CLONE_NEWNS,	.name = "ns/mnt", .fd = -1 },
	{ .nstype = 0, .name = NULL, .fd = -1 }
};

static int snprintf_realloc(char **str, size_t *size,
		size_t offset, const char *format, ...)
{
	int ret, needed;
	char *tmp;
	va_list ap;

	va_start(ap, format);
	while (0 <= (ret = vsnprintf(*str + offset, *size - offset,
				format, ap))
		  && *size - offset < (needed = ret + 1)) {
		va_end(ap);

		while (*size - offset < needed) {
			*size *= 2;
		}

		tmp = realloc(*str, *size);

		if (tmp) {
			*str = tmp;
		} else {
			free(*str);
			*str = NULL;
			return -1;
		}
		va_start(ap, format);
	}
	va_end(ap);

	return ret;
}

/* Return number of CPUs on success, negative on failure */
static int cpu_str2array(char *_cpu_list, int num_cpus, int *cpus)
{
	int ret = 0;
	int i;
	int cpu_rank = 0;
	char *cpu_list, *to_free = NULL;
	char *token, *minus;

	if (!_cpu_list) {
		/* nothing to do */
		ret = 0;
		goto out;
	}

	if (!(cpu_list = strdup(_cpu_list))) {
		eprintf("%s: error: allocating cpu_list\n",
			__func__);
		ret = -errno;
		goto out;
	}
	to_free = cpu_list;

	token = strsep(&cpu_list, ",");
	while (token) {
		if (*token == 0) {
			eprintf("%s: error: illegal expression: %s\n",
				__func__, _cpu_list); /* empty token */
			ret = -EINVAL;
			goto out;
		}
		if ((minus = strchr(token, '-'))) {
			int start, end;

			if (*(minus + 1) == 0) {
				eprintf("%s: error: illegal expression: %s\n",
					__func__, _cpu_list); /* empty token */
				ret = -EINVAL;
				goto out;
			}
			*minus = 0;
			start = atoi(token);
			end = atoi(minus + 1);
			for (i = start; i <= end; i++) {
				dprintf("%s: cpus[%d]=%d\n",
					__func__, cpu_rank, i);
				if (cpus && num_cpus > cpu_rank) {
					cpus[cpu_rank] = i;
				}
				cpu_rank++;
			}
		}
		else {
			dprintf("%s: cpus[%d]=%d\n",
				__func__, cpu_rank, atoi(token));
			if (cpus && num_cpus > cpu_rank) {
				cpus[cpu_rank] = atoi(token);
			}
			cpu_rank++;
		}
		token = strsep(&cpu_list, ",");
	}

	ret = cpu_rank;

 out:
	free(to_free);
	return ret;
}

int cpu_str2count(char *cpu_list)
{
	return cpu_str2array(cpu_list, 0, NULL);
}

/* Return number of CPUs on success, negative on failure */
int cpu_str2req(char *_cpu_list, int num_cpus, struct ihk_cpu_req *req)
{
	int ret = 0;

	if (!req) {
		eprintf("%s: error: invalid req pointer (NULL)\n", __func__);
		ret = -EINVAL;
		goto out;
	}

	ret = cpu_str2array(_cpu_list, num_cpus, req->cpus);
	req->num_cpus = ret;

 out:
	return ret;
}

static int mem_str2array(char *mem_list, int *num_mem_chunks,
			struct ihk_mem_chunk *mem_chunks)
{
	int ret = 0;
	int mem_count = 0;
	char *chunk = mem_list;
	char *token = strsep(&chunk, ",");
	while (token != NULL) {
		if(*token == 0) {
			goto empty_mem;
		}
		char* cdr = token;
		token = strsep(&cdr, "@");
		if (mem_chunks && *num_mem_chunks > mem_count) {
			mem_chunks[mem_count].size = atol(token);
			if (cdr != NULL) {
				mem_chunks[mem_count].numa_node_number = atol(cdr);
			}
		}
		mem_count++;
	empty_mem:
		token = strsep(&chunk, ",");
	}

	ret = mem_count;

    return ret;
}

/* Return number of maps on success, negative on failure */
static int ikc_str2array(char *_ikc_list, int num_maps,
		int *src_cpus, int *dst_cpus)
{
	int ret = 0;
	int i;
	int token_cnt = 0, total_cnt = 0;
	int cpu_buf[IHK_MAX_NUM_CPUS] = {0};
	char *ikc_list, *to_free = NULL;
	char *token;

	if (!_ikc_list) {
		/* nothing to do */
		ret = 0;
		goto out;
	}

	if (!(ikc_list = strdup(_ikc_list))) {
		eprintf("%s: error: allocating ikc_list\n",
			__func__);
		ret = -errno;
		goto out;
	}
	to_free = ikc_list;

	token = strsep(&ikc_list, "+");
	while (token) {
		char *cpu_list;
		char *ikc_cpu;
		int dst_cpu;

		cpu_list = strsep(&token, ":");
		if (!cpu_list) {
			ret = -EINVAL;
			goto out;
		}

		token_cnt = cpu_str2array(cpu_list, IHK_MAX_NUM_CPUS, cpu_buf);

		ikc_cpu = strsep(&token, ":");
		if (!ikc_cpu) {
			ret = -EINVAL;
			goto out;
		}

		dst_cpu = atoi(ikc_cpu);

		/* Store IKC target CPU */
		for (i = 0; i < token_cnt; i++) {
			if (src_cpus && dst_cpus && num_maps > total_cnt) {
				src_cpus[total_cnt] = cpu_buf[i];
				dst_cpus[total_cnt] = dst_cpu;
			}
			total_cnt++;
		}

		token = strsep(&ikc_list, "+");
	}

	ret = total_cnt;

 out:
	free(to_free);
	return ret;
}

int ikc_str2count(char *_ikc_list)
{
	return ikc_str2array(_ikc_list, 0, NULL, NULL);
}

int ikc_str2req(char *_ikc_list, int num_cpus, struct ihk_ikc_req *req)
{
	int ret = 0;

	if (!req) {
		eprintf("%s: error: invalid req pointer (NULL)\n", __func__);
		ret = -EINVAL;
		goto out;
	}

	ret = ikc_str2array(_ikc_list, num_cpus, req->src_cpus, req->dst_cpus);
	req->num_cpus = ret;

 out:
	return ret;
}


static size_t ihk_memparse(char *token)
{
	size_t ret;
	char *endp = token + strlen(token) - 1;

	ret = atol(token);

	switch (*endp) {
	case 'e':
	case 'E':
		ret <<= 10;
	case 'p':
	case 'P':
		ret <<= 10;
	case 't':
	case 'T':
		ret <<= 10;
	case 'g':
	case 'G':
		ret <<= 10;
	case 'm':
	case 'M':
		ret <<= 10;
	case 'k':
	case 'K':
		ret <<= 10;
	default:
		// do nothing
		break;
	}

	return ret;
}

/* Return number of MEM chunks on success, negative on failure */
int mem_str2req(char *_mem_list, int num_mem_chunks, struct ihk_mem_req *req)
{
	int ret = 0;
	int mem_count = 0;
	char *mem_list, *to_free = NULL;
	char *token, *cdr;

	if (!_mem_list) {
		/* nothing to do */
		ret = 0;
		goto out;
	}

	if (!(mem_list = strdup(_mem_list))) {
		eprintf("%s: error: allocating mem_list\n",
			__func__);
		ret = -errno;
		goto out;
	}
	to_free = mem_list;

	token = strsep(&mem_list, ",");
	while (token) {
		if (*token == 0) {
			eprintf("%s: error: illegal expression: %s\n",
				__func__, _mem_list); /* empty token */
			ret = -EINVAL;
			goto out;
		}
		cdr = token;
		token = strsep(&cdr, "@");
		if (req && num_mem_chunks > mem_count) {
			req->sizes[mem_count] = ihk_memparse(token);
			if (cdr != NULL) {
				req->numa_ids[mem_count] = atol(cdr);
			}
		}
		mem_count++;

		token = strsep(&mem_list, ",");
	}

	if (req) {
		req->num_chunks = mem_count;
	}

	ret = mem_count;

 out:
	free(to_free);
	return ret;
}

int mem_str2count(char *mem_list)
{
	return mem_str2req(mem_list, 0, NULL);
}

static char *cpu_array2str(int num_cpus, int *cpus)
{
	/* prev_cpu should be < -1 so that "if (prev_cpu != cpus[i] - 1)"
	 * won't misunderstand that the cursor is pointing to "0"
	 * following "-1".
	 */
	int i, prev_cpu = -10, in_seq = 0, n = 0;
	size_t buflen = 64;
	char *str = NULL;

	str = malloc(buflen);
	if (!str) {
		goto out;
	}

	memset(str, 0, buflen);

	for (i = 0; i < num_cpus; i++) {
		if (prev_cpu != cpus[i] - 1) {
			if (prev_cpu > 0) {
				n += snprintf_realloc(&str, &buflen, n,
					"%d,", prev_cpu);
			}
			in_seq = 0;
		}
		else {
			if (!in_seq) {
				n += snprintf_realloc(&str, &buflen, n,
					"%d-", prev_cpu);
				in_seq = 1;
			}
		}

		prev_cpu = cpus[i];
	}

	if (prev_cpu >= 0) {
		n += snprintf_realloc(&str, &buflen, n,
			"%d", prev_cpu);
	}

 out:
	return str;
}

char *cpu_req2str(struct ihk_cpu_req *req)
{
	return cpu_array2str(req->num_cpus, req->cpus);
}

static char *mem_array2str(int num_mem_chunks, size_t *sizes, int *numa_ids)
{
	int i, n = 0;
	size_t buflen = 64;
	char *str = NULL;

	str = malloc(buflen);
	if (!str) {
		goto out;
	}

	memset(str, 0, buflen);

	for (i = 0; i < num_mem_chunks; i++) {
		n += snprintf_realloc(&str, &buflen, n,
			"%lu@%d", sizes[i], numa_ids[i]);
		if (i != num_mem_chunks - 1) {
			n += snprintf_realloc(&str, &buflen, n, ",");
		}
	}

 out:
	return str;
}

char *mem_req2str(struct ihk_mem_req *req)
{
	return mem_array2str(req->num_chunks, req->sizes, req->numa_ids);
}

char *ikc_req2str(struct ihk_ikc_req *req)
{
	int i, src, dst, max_dst = -1, idx, n = 0;
	char *str = NULL;
	size_t buflen = 64;

	/* Sender-set (sset): Set of senders sharing the same destination */
	int *rank = NULL; /* Order in sender-set, indexed by IKC source CPU# */
	int *ikc_sset_sizes = NULL; /* Indexed by IKC destination CPU# */
	int **ikc_sset_members = NULL; /* Indexed by IKC destination CPU# */

	str = malloc(buflen);
	if (!str) {
		goto out;
	}

	memset(str, 0, buflen);

	rank = calloc(sizeof(int), IHK_MAX_NUM_CPUS);
	if (!rank) {
		eprintf("%s: error: allocating rank\n", __func__);
		goto out;
	}

	ikc_sset_sizes = calloc(sizeof(int), IHK_MAX_NUM_CPUS);
	if (!ikc_sset_sizes) {
		eprintf("%s: error: allocating num_ikc_ssets\n", __func__);
		goto out;
	}

	ikc_sset_members = calloc(sizeof(int *), IHK_MAX_NUM_CPUS);
	if (!ikc_sset_members) {
		eprintf("%s: error: allocating ikc_sset_members\n", __func__);
		goto out;
	}

	for (idx = 0; idx < req->num_cpus; idx++) {
		src = req->src_cpus[idx];
		dst = req->dst_cpus[idx];

		rank[src] = ikc_sset_sizes[dst];
		ikc_sset_sizes[dst]++;
		if (max_dst < dst) {
			max_dst = dst;
		}
	}

	for (idx = 0; idx < req->num_cpus; idx++) {
		src = req->src_cpus[idx];
		dst = req->dst_cpus[idx];

		if (!ikc_sset_members[dst]) {
			ikc_sset_members[dst] = calloc(sizeof(int),
					ikc_sset_sizes[dst]);
			if (!ikc_sset_members[dst]) {
				eprintf("%s: error: allocating ikc_sset_members\n",
					__func__);
				goto out;
			}
		}
		*(ikc_sset_members[dst] + rank[src]) = src;
	}

	for (dst = 0; dst < IHK_MAX_NUM_CPUS; dst++) {
		if (ikc_sset_sizes[dst] == 0) {
			continue;
		}

		for (i = 0; i < ikc_sset_sizes[dst]; i++) {
			n += snprintf_realloc(&str, &buflen, n,
				"%d", *(ikc_sset_members[dst] + i));
			if (i != ikc_sset_sizes[dst] - 1) {
				n += snprintf_realloc(&str, &buflen, n, ",");
			}
		}
		n += snprintf_realloc(&str, &buflen, n, ":%d", dst);
		if (dst != max_dst) {
			n += snprintf_realloc(&str, &buflen, n, "+");
		}
	}

	dprintf("get_ikc_map,query_res=%s\n", str);

out:
	if (ikc_sset_members) {
		for (dst = 0; dst < IHK_MAX_NUM_CPUS; ++dst) {
			free(ikc_sset_members[dst]);
		}
	}

	free(ikc_sset_members);
	free(ikc_sset_sizes);
	free(rank);

	return str;
}

int ihklib_device_open(int index)
{
	int ret = 0;
	char fn[PATH_MAX];
	struct stat file_stat;

	sprintf(fn, "/dev/mcd%d", index);
	if ((ret = stat(fn, &file_stat))) {
		eprintf("%s: error: stat %s: %s\n",
			__func__, fn, strerror(errno));
		ret = -errno;
		goto out;
	}

	if ((ret = open(fn, O_RDONLY)) == -1) {
		eprintf("%s: error: open %s: %s\n",
			__func__, fn, strerror(errno));
		ret = -errno;
		goto out;
	}

 out:
	return ret;
}

int ihk_reserve_cpu(int index, int* cpus, int num_cpus)
{
	int ret = 0, ret_ioctl;
	char cpu_list[IHK_MAX_NUM_CPUS];
	struct ihk_ioctl_cpu_desc req = { 0 };
	int fd = -1;

	CHKANDJUMP(num_cpus > IHK_MAX_NUM_CPUS, -EINVAL, "too many cpus requested\n");

	if ((fd = ihklib_device_open(index)) < 0) {
		eprintf("%s: error: ihklib_device_open\n",
			__func__);
		ret = fd;
		goto out;
	}

	req.cpus = cpus;
	req.num_cpus = num_cpus;

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
	int ret = 0;
	int fd = -1;

	if ((fd = ihklib_device_open(index)) < 0) {
		eprintf("%s: error: ihklib_device_open\n",
			__func__);
		ret = fd;
		goto out;
	}

	ret = ioctl(fd, IHK_DEVICE_GET_NUM_CPUS);
	CHKANDJUMP(ret < 0, -errno, "ioctl failed\n");

 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}

int ihk_query_cpu(int index, int *cpus, int num_cpus)
{
	int ret = 0;
	struct ihk_ioctl_cpu_desc req = { 0 };
	int fd = -1;

	if (num_cpus > IHK_MAX_NUM_CPUS) {
		eprintf("%s: error: too many cpus (%d) requested\n",
			__func__, num_cpus);
		ret = -EINVAL;
		goto out;
	}

	if ((fd = ihklib_device_open(index)) < 0) {
		eprintf("%s: error: ihklib_device_open\n",
			__func__);
		ret = fd;
		goto out;
	}

	if ((ret = ioctl(fd, IHK_DEVICE_GET_NUM_CPUS)) < 0) {
		eprintf("%s: error: IHK_DEVICE_GET_NUM_CPUS\n",
			__func__);
		ret = -errno;
		goto out;
	}

	if (ret != num_cpus) {
		fprintf(stderr,
			"%s: error: actual number of CPUs (%d) is different than requested (%d)\n",
			__func__, ret, num_cpus);
		ret = -EINVAL;
		goto out;
	}

	req.cpus = cpus;
	req.num_cpus = num_cpus;
	CHKANDJUMP(!req.cpus || !req.num_cpus, -EINVAL, "invalid format\n");

	if ((ret = ioctl(fd, IHK_DEVICE_QUERY_CPU, &req))) {
		eprintf("%s: error: IHK_DEVICE_QUERY_CPU\n",
			__func__);
		ret = -errno;
		goto out;
	}

 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}

int ihk_release_cpu(int index, int* cpus, int num_cpus)
{
	int ret = 0, ret_ioctl;
	struct ihk_ioctl_cpu_desc req = { 0 };
	int fd = -1;

	CHKANDJUMP(num_cpus > IHK_MAX_NUM_CPUS, -EINVAL,
		"too many cpus specified\n");

	if ((fd = ihklib_device_open(index)) < 0) {
		eprintf("%s: error: ihklib_device_open\n",
			__func__);
		ret = fd;
		goto out;
	}

	req.cpus = cpus;
	req.num_cpus = num_cpus;
	CHKANDJUMP(!req.cpus || !req.num_cpus, -EINVAL,
		"invalid format\n");

	ret_ioctl = ioctl(fd, IHK_DEVICE_RELEASE_CPU, &req);
	CHKANDJUMP(ret_ioctl != 0, -errno, "ioctl failed\n");

 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}

int ihk_reserve_mem(int index, struct ihk_mem_chunk* mem_chunks, int num_mem_chunks)
{
	int ret = 0, ret_ioctl, i;
	struct ihk_ioctl_mem_desc req = { 0 };
	int fd = -1;

	CHKANDJUMP(num_mem_chunks > IHK_MAX_NUM_MEM_CHUNKS, -EINVAL, "too many memory chunks requested\n");

	if ((fd = ihklib_device_open(index)) < 0) {
		eprintf("%s: error: ihklib_device_open\n",
			__func__);
		ret = fd;
		goto out;
	}

	CHKANDJUMP(!mem_chunks || !num_mem_chunks, -EINVAL, "invalid format\n");

	req.sizes = calloc(num_mem_chunks, sizeof(size_t));
	if (!req.sizes) {
		eprintf("%s: error: allocating request sizes\n",
			__func__);
		ret = -ENOMEM;
		goto out;
	}

	req.numa_ids = calloc(num_mem_chunks, sizeof(int));
	if (!req.numa_ids) {
		eprintf("%s: error: allocating request numa_ids\n",
			__func__);
		ret = -ENOMEM;
		goto out;
	}

	for (i = 0; i < num_mem_chunks; i++) {
		req.sizes[i] = (size_t)mem_chunks[i].size;
		req.numa_ids[i] = mem_chunks[i].numa_node_number;
	}
	req.num_chunks = num_mem_chunks;

	ret_ioctl = ioctl(fd, IHK_DEVICE_RESERVE_MEM, &req);
	CHKANDJUMP(ret_ioctl != 0, -errno, "ioctl failed");

 out:
	if (fd != -1) {
		close(fd);
	}
	free(req.sizes);
	free(req.numa_ids);
	return ret;
}

int ihk_get_num_reserved_mem_chunks(int index)
{
	int ret = 0, ret_ioctl;
	int fd = -1;
	struct ihk_ioctl_mem_desc req = { 0 };

	if ((fd = ihklib_device_open(index)) < 0) {
		eprintf("%s: error: ihklib_device_open\n",
			__func__);
		ret = fd;
		goto out;
	}

	req.num_chunks = 0;   /* means only get num_reserved_mem_chunks */

	ret_ioctl = ioctl(fd, IHK_DEVICE_QUERY_MEM, &req);
	CHKANDJUMP(ret_ioctl != 0, -errno, "ioctl failed\n");

	ret = req.num_chunks;

 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}

int ihk_query_mem(int index, struct ihk_mem_chunk* mem_chunks, int _num_mem_chunks)
{
	int ret = 0, ret_ioctl;
	int fd = -1;
	int i;
	struct ihk_ioctl_mem_desc req = { 0 };

	if ((fd = ihklib_device_open(index)) < 0) {
		eprintf("%s: error: ihklib_device_open\n",
			__func__);
		ret = fd;
		goto out;
	}

	req.sizes = calloc(_num_mem_chunks, sizeof(size_t));
	if (!req.sizes) {
		eprintf("%s: error: allocating request sizes\n",
			__func__);
		ret = -ENOMEM;
		goto out;
	}

	req.numa_ids = calloc(_num_mem_chunks, sizeof(int));
	if (!req.numa_ids) {
		eprintf("%s: error: allocating request numa_ids\n",
			__func__);
		ret = -ENOMEM;
		goto out;
	}

	req.num_chunks = _num_mem_chunks;

	ret_ioctl = ioctl(fd, IHK_DEVICE_QUERY_MEM, &req);
	CHKANDJUMP(ret_ioctl != 0, -errno, "ioctl failed\n");

	CHKANDJUMP(req.num_chunks != _num_mem_chunks, -EINVAL,
		   "actual number of memory chunks (%d) is different than requested (%d)\n",
		   req.num_chunks, _num_mem_chunks);

	for (i = 0; i < _num_mem_chunks; i++) {
		mem_chunks[i].size = req.sizes[i];
		mem_chunks[i].numa_node_number = req.numa_ids[i];
	}

 out:
	if (fd != -1) {
		close(fd);
	}
	free(req.sizes);
	free(req.numa_ids);
	return ret;
}

int ihk_release_mem(int index, struct ihk_mem_chunk* mem_chunks, int num_mem_chunks)
{
	int ret = 0, i, ret_ioctl;
	struct ihk_ioctl_mem_desc req = { 0 };
	int fd = -1;

	CHKANDJUMP(num_mem_chunks > IHK_MAX_NUM_MEM_CHUNKS, -EINVAL,
		"too many memory chunks specified\n");

	if ((fd = ihklib_device_open(index)) < 0) {
		eprintf("%s: error: ihklib_device_open\n",
			__func__);
		ret = fd;
		goto out;
	}

	CHKANDJUMP(!mem_chunks || !num_mem_chunks, -EINVAL,
		"invalid format\n");

	req.sizes = calloc(num_mem_chunks, sizeof(size_t));
	if (!req.sizes) {
		eprintf("%s: error: allocating request sizes\n",
			__func__);
		ret = -ENOMEM;
		goto out;
	}

	req.numa_ids = calloc(num_mem_chunks, sizeof(int));
	if (!req.numa_ids) {
		eprintf("%s: error: allocating request numa_ids\n",
			__func__);
		ret = -ENOMEM;
		goto out;
	}

	for (i = 0; i < num_mem_chunks; i++) {
		req.sizes[i] = (size_t)mem_chunks[i].size;
		req.numa_ids[i] = mem_chunks[i].numa_node_number;
	}
	req.num_chunks = num_mem_chunks;

	ret_ioctl = ioctl(fd, IHK_DEVICE_RELEASE_MEM, &req);
	CHKANDJUMP(ret_ioctl != 0, -errno, "ioctl failed");

 out:
	if (fd != -1) {
		close(fd);
	}
	free(req.sizes);
	free(req.numa_ids);
	return ret;
}

/* Create OS and return OS index */
int ihk_create_os(int index)
{
	int ret = 0, ret_ioctl;
	int fd = -1;

	if ((fd = ihklib_device_open(index)) < 0) {
		eprintf("%s: error: ihklib_device_open\n",
			__func__);
		ret = fd;
		goto out;
	}

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

	if ((fd = ihklib_device_open(index)) < 0) {
		eprintf("%s: error: ihklib_device_open\n",
			__func__);
		ret = fd;
		goto out;
	}

	dir = opendir(PATH_DEV);
	CHKANDJUMP(dir == NULL, -EINVAL, "opendir failed\n");

	direp = readdir(dir);
	while (direp) {
		if ((strncmp(direp->d_name,"mcos",4) == 0)) {
			num_os_instances++;
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
			indices[num_os_instances] = atoi(direp->d_name + 4);
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

	if ((fd = ihklib_device_open(dev_index)) == -1) {
		eprintf("%s: error: ihklib_device_open\n",
			__func__);
		ret = fd;
		goto out;
	}

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
	int ret = 0;
	char fn[PATH_MAX];
	struct stat file_stat;

	sprintf(fn, "/dev/mcos%d", index);
	if ((ret = stat(fn, &file_stat))) {
		eprintf("%s: error: stat %s: %s\n",
			__func__, fn, strerror(errno));
		ret = -errno;
		goto out;
	}

	if ((ret = open(fn, O_RDONLY)) == -1) {
		eprintf("%s: error: open %s: %s\n",
			__func__, fn, strerror(errno));
		ret = -errno;
		goto out;
	}

 out:
	return ret;
}

int ihk_os_assign_cpu(int index, int* cpus, int num_cpus)
{
	int ret = 0, ret_ioctl;
	struct ihk_ioctl_cpu_desc req = { 0 };
	int fd = -1;

	CHKANDJUMP(num_cpus > IHK_MAX_NUM_CPUS, -EINVAL,
		"too many cpus requested\n");

	if ((fd = ihklib_os_open(index)) < 0) {
		eprintf("%s: error: ihklib_os_open\n",
			__func__);
		ret = fd;
		goto out;
	}

	req.cpus = cpus;
	req.num_cpus = num_cpus;
	CHKANDJUMP(!req.cpus || !req.num_cpus, -EINVAL,
		"invalid format\n");

	ret_ioctl = ioctl(fd, IHK_OS_ASSIGN_CPU, &req);
	CHKANDJUMP(ret_ioctl != 0, -errno, "ioctl failed\n");

 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}

int ihk_os_get_num_assigned_cpus(int index)
{
	int ret = 0;
	int fd = -1;

	if ((fd = ihklib_os_open(index)) < 0) {
		eprintf("%s: error: ihklib_os_open\n",
			__func__);
		ret = fd;
		goto out;
	}

	ret = ioctl(fd, IHK_OS_GET_NUM_CPUS);
	CHKANDJUMP(ret < 0, -errno, "ioctl failed\n");

 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}

int ihk_os_query_cpu(int index, int *cpus, int num_cpus)
{
	int ret = 0;
	struct ihk_ioctl_cpu_desc req = { 0 };
	int fd = -1;

	if (num_cpus > IHK_MAX_NUM_CPUS) {
		eprintf("%s: error: too many cpus (%d) requested\n",
			__func__, num_cpus);
		ret = -EINVAL;
		goto out;
	}

	if ((fd = ihklib_os_open(index)) < 0) {
		eprintf("%s: error: ihklib_os_open\n",
			__func__);
		ret = fd;
		goto out;
	}

	if ((ret = ioctl(fd, IHK_OS_GET_NUM_CPUS)) < 0) {
		eprintf("%s: error: IHK_OS_GET_NUM_CPUS returned %d\n",
			__func__, errno);
		ret = -errno;
		goto out;
	}

	if (ret != num_cpus) {
		fprintf(stderr,
			"%s: error: actual number of CPUs (%d) is different than requested (%d)\n",
			__func__, ret, num_cpus);
		ret = -EINVAL;
		goto out;
	}

	req.cpus = cpus;
	req.num_cpus = num_cpus;
	CHKANDJUMP(!req.cpus || !req.num_cpus, -EINVAL, "invalid format\n");

	if ((ret = ioctl(fd, IHK_OS_QUERY_CPU, &req))) {
		eprintf("%s: error: IHK_OS_QUERY_CPU returned %d\n",
			__func__, errno);
		ret = -errno;
		goto out;
	}

 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}

int ihk_os_release_cpu(int index, int *cpus, int num_cpus)
{
	int ret = 0, ret_ioctl;
	struct ihk_ioctl_cpu_desc req = { 0 };
	int fd = -1;

	CHKANDJUMP(num_cpus > IHK_MAX_NUM_CPUS, -EINVAL,
		"too many cpus specified\n");

	if ((fd = ihklib_os_open(index)) < 0) {
		eprintf("%s: error: ihklib_os_open\n",
			__func__);
		ret = fd;
		goto out;
	}

	req.cpus = cpus;
	req.num_cpus = num_cpus;
	CHKANDJUMP(!req.cpus || !req.num_cpus, -EINVAL,
		"invalid format\n");

	ret_ioctl = ioctl(fd, IHK_OS_RELEASE_CPU, &req);
	CHKANDJUMP(ret_ioctl != 0, -errno, "ioctl failed\n");

 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}

int ihk_os_set_ikc_map(int index, struct ihk_ikc_cpu_map *map, int num_cpus)
{
	int ret = 0, i, ret_ioctl;
	struct ihk_ioctl_ikc_desc req = { 0 };
	int fd = -1;

	CHKANDJUMP(num_cpus > IHK_MAX_NUM_CPUS, -EINVAL,
		"too many cpus specified\n");

	if ((fd = ihklib_os_open(index)) < 0) {
		eprintf("%s: error: ihklib_os_open\n",
			__func__);
		ret = fd;
		goto out;
	}

	req.src_cpus = calloc(num_cpus, sizeof(int));
	if (!req.src_cpus) {
		eprintf("%s: error: allocating request src_cpus\n",
			__func__);
		ret = -ENOMEM;
		goto out;
	}

	req.dst_cpus = calloc(num_cpus, sizeof(int));
	if (!req.dst_cpus) {
		eprintf("%s: error: allocating request dst_cpuss\n",
			__func__);
		ret = -ENOMEM;
		goto out;
	}

	for (i = 0; i < num_cpus; i++) {
		req.src_cpus[i] = map[i].src_cpu;
		req.dst_cpus[i] = map[i].dst_cpu;
	}
	req.num_cpus = num_cpus;

	ret_ioctl = ioctl(fd, IHK_OS_SET_IKC_MAP, &req);
	CHKANDJUMP(ret_ioctl != 0, -errno, "ioctl failed\n");

 out:
	if (fd != -1) {
		close(fd);
	}
	free(req.src_cpus);
	free(req.dst_cpus);
	return ret;
}

int ihk_os_get_ikc_map(int index, struct ihk_ikc_cpu_map *map, int num_cpus)
{
	int ret = 0, i, ret_ioctl;
	struct ihk_ioctl_ikc_desc req = { 0 };
	int fd = -1;

	CHKANDJUMP(num_cpus > IHK_MAX_NUM_CPUS, -EINVAL,
		"too many cpus specified\n");

	if ((fd = ihklib_os_open(index)) < 0) {
		eprintf("%s: error: ihklib_os_open\n",
			__func__);
		ret = fd;
		goto out;
	}

	req.src_cpus = calloc(num_cpus, sizeof(int));
	if (!req.src_cpus) {
		eprintf("%s: error: allocating request src_cpus\n",
			__func__);
		ret = -ENOMEM;
		goto out;
	}

	req.dst_cpus = calloc(num_cpus, sizeof(int));
	if (!req.dst_cpus) {
		eprintf("%s: error: allocating request dst_cpuss\n",
			__func__);
		ret = -ENOMEM;
		goto out;
	}

	req.num_cpus = num_cpus;

	ret_ioctl = ioctl(fd, IHK_OS_GET_IKC_MAP, &req);
	CHKANDJUMP(ret_ioctl != 0, -errno, "ioctl failed\n");

	CHKANDJUMP(req.num_cpus != num_cpus, -EINVAL,
		   "actual number of ikc_maps (%d) is different than requested (%d)\n",
		   req.num_cpus, num_cpus);

	for (i = 0; i < req.num_cpus; i++) {
		map[i].src_cpu = req.src_cpus[i];
		map[i].dst_cpu = req.dst_cpus[i];
	}

	ret = 0;

 out:
	if (fd != -1) {
		close(fd);
	}
	free(req.src_cpus);
	free(req.dst_cpus);
	return ret;
}

int ihk_os_assign_mem(int index, struct ihk_mem_chunk *mem_chunks, int num_mem_chunks)
{
	int ret = 0, ret_ioctl, i;
	struct ihk_ioctl_mem_desc req = { 0 };
	int fd = -1;

	CHKANDJUMP(num_mem_chunks > IHK_MAX_NUM_MEM_CHUNKS, -EINVAL, "too many memory chunks requested\n");

	if ((fd = ihklib_os_open(index)) < 0) {
		eprintf("%s: error: ihklib_os_open\n",
			__func__);
		ret = fd;
		goto out;
	}

	CHKANDJUMP(!mem_chunks || !num_mem_chunks, -EINVAL,
		"invalid format\n");

	req.sizes = calloc(num_mem_chunks, sizeof(size_t));
	if (!req.sizes) {
		eprintf("%s: error: allocating request sizes\n",
			__func__);
		ret = -ENOMEM;
		goto out;
	}

	req.numa_ids = calloc(num_mem_chunks, sizeof(int));
	if (!req.numa_ids) {
		eprintf("%s: error: allocating request numa_ids\n",
			__func__);
		ret = -ENOMEM;
		goto out;
	}

	for (i = 0; i < num_mem_chunks; i++) {
		req.sizes[i] = (size_t)mem_chunks[i].size;
		req.numa_ids[i] = mem_chunks[i].numa_node_number;
	}
	req.num_chunks = num_mem_chunks;

	ret_ioctl = ioctl(fd, IHK_OS_ASSIGN_MEM, &req);
	CHKANDJUMP(ret_ioctl != 0, -errno, "ioctl failed");

 out:
	if (fd != -1) {
		close(fd);
	}
	free(req.sizes);
	free(req.numa_ids);
	return ret;
}

int ihk_os_get_num_assigned_mem_chunks(int index)
{
	int ret = 0, ret_ioctl;
	struct ihk_ioctl_mem_desc req = { 0 };
	int fd = -1;

	if ((fd = ihklib_os_open(index)) < 0) {
		eprintf("%s: error: ihklib_os_open\n",
			__func__);
		ret = fd;
		goto out;
	}

	req.num_chunks = 0;   /* means only get num_chunks */

	ret_ioctl = ioctl(fd, IHK_OS_QUERY_MEM, &req);
	CHKANDJUMP(ret_ioctl != 0, -errno, "ioctl failed\n");

	ret = req.num_chunks;

 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}

int ihk_os_query_mem(int index, struct ihk_mem_chunk* mem_chunks, int _num_mem_chunks)
{
	int ret = 0, ret_ioctl, i;
	struct ihk_ioctl_mem_desc req = { 0 };
	int fd = -1;

	if ((fd = ihklib_os_open(index)) < 0) {
		eprintf("%s: error: ihklib_os_open\n",
			__func__);
		ret = fd;
		goto out;
	}

	req.sizes = calloc(_num_mem_chunks, sizeof(size_t));
	if (!req.sizes) {
		eprintf("%s: error: allocating request sizes\n",
			__func__);
		ret = -ENOMEM;
		goto out;
	}

	req.numa_ids = calloc(_num_mem_chunks, sizeof(int));
	if (!req.numa_ids) {
		eprintf("%s: error: allocating request numa_ids\n",
			__func__);
		ret = -ENOMEM;
		goto out;
	}

	req.num_chunks = _num_mem_chunks;

	ret_ioctl = ioctl(fd, IHK_OS_QUERY_MEM, &req);
	CHKANDJUMP(ret_ioctl != 0, -errno, "ioctl failed\n");

	CHKANDJUMP(req.num_chunks != _num_mem_chunks, -EINVAL,
		   "actual number of memory chunks (%d) is different than requested (%d)\n",
		   req.num_chunks, _num_mem_chunks);

	for (i = 0; i < _num_mem_chunks; i++) {
		mem_chunks[i].size = req.sizes[i];
		mem_chunks[i].numa_node_number = req.numa_ids[i];
	}

	ret = 0;

 out:
	if (fd != -1) {
		close(fd);
	}
	free(req.sizes);
	free(req.numa_ids);
	return ret;
}

int ihk_os_release_mem(int index, struct ihk_mem_chunk *mem_chunks,
		int num_mem_chunks)
{
	int ret = 0, i, ret_ioctl;
	struct ihk_ioctl_mem_desc req = { 0 };
	int fd = -1;

	CHKANDJUMP(num_mem_chunks > IHK_MAX_NUM_MEM_CHUNKS, -EINVAL,
		"too many memory chunks specified\n");

	if ((fd = ihklib_os_open(index)) < 0) {
		eprintf("%s: error: ihklib_os_open\n",
			__func__);
		ret = fd;
		goto out;
	}

	req.sizes = calloc(num_mem_chunks, sizeof(size_t));
	if (!req.sizes) {
		eprintf("%s: error: allocating request sizes\n",
			__func__);
		ret = -ENOMEM;
		goto out;
	}

	req.numa_ids = calloc(num_mem_chunks, sizeof(int));
	if (!req.numa_ids) {
		eprintf("%s: error: allocating request numa_ids\n",
			__func__);
		ret = -ENOMEM;
		goto out;
	}

	for (i = 0; i < num_mem_chunks; i++) {
		req.sizes[i] = (size_t)mem_chunks[i].size;
		req.numa_ids[i] = mem_chunks[i].numa_node_number;
	}
	req.num_chunks = num_mem_chunks;

	ret_ioctl = ioctl(fd, IHK_OS_RELEASE_MEM, &req);
	CHKANDJUMP(ret_ioctl != 0, -errno, "ioctl failed");

 out:
	if (fd != -1) {
		close(fd);
	}
	free(req.sizes);
	free(req.numa_ids);
	return ret;
}

int ihk_os_get_eventfd(int index, int type)
{
	int fd = -1;
	int ret = 0, ret_ioctl;
	struct ihk_os_ioctl_eventfd_desc desc;

	memset(&desc, 0, sizeof(desc));

	if ((fd = ihklib_os_open(index)) < 0) {
		eprintf("%s: error: ihklib_os_open\n",
			__func__);
		ret = fd;
		goto out;
	}

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

	if ((fd = ihklib_os_open(index)) < 0) {
		eprintf("%s: error: ihklib_os_open\n",
			__func__);
		ret = fd;
		goto out;
	}

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

	if ((fd = ihklib_os_open(index)) < 0) {
		eprintf("%s: error: ihklib_os_open\n",
			__func__);
		ret = fd;
		goto out;
	}

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
	int ret = 0;
	int fd = -1;
	int i;
	char query_result[1024];

	if ((fd = ihklib_os_open(index)) < 0) {
		eprintf("%s: error: ihklib_os_open\n",
			__func__);
		ret = fd;
		goto out;
	}

	if ((ret = ioctl(fd, IHK_OS_BOOT, 0)) == -1) {
		dprintf("error: ioctl failed\n");
		ret = -errno;
		goto out;
	}

	for (i = 0; i < 100; i++) { /* 10 second */
		ret = ioctl(fd, IHK_OS_STATUS, query_result);

		switch (ret) {
		case IHK_OS_STATUS_BOOTING:
		case IHK_OS_STATUS_BOOTED:
		case IHK_OS_STATUS_READY:
			usleep(100000);
			continue;
		default:
			goto booted_or_error;
		}
	}

booted_or_error:

	if (ret == -1) {
		eprintf("%s: error: IHK_OS_STATUS\n",
			__func__);
		ret = -errno;
		goto out;
	}

	if (ret != IHK_OS_STATUS_RUNNING) {
		eprintf("%s: error: Invalid status: %d\n",
			__func__, ret);
		ret = -EINVAL;
		goto out;
	}

	ret = 0;
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

	if ((fd = ihklib_os_open(index)) < 0) {
		eprintf("%s: error: ihklib_os_open\n",
			__func__);
		ret = fd;
		goto out;
	}

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

	if ((fd = ihklib_os_open(index)) < 0) {
		eprintf("%s: error: ihklib_os_open\n",
			__func__);
		ret = fd;
		goto out;
	}

	memset(query_result, 0, sizeof(query_result));

	ret_ioctl = ioctl(fd, IHK_OS_STATUS, query_result);
	CHKANDJUMP(ret_ioctl < 0, -errno, "ioctl failed\n");

	switch (ret_ioctl) {
	case IHK_OS_STATUS_NOT_BOOTED:
		ret = IHK_STATUS_INACTIVE;
		break;
	case IHK_OS_STATUS_BOOTING:
	case IHK_OS_STATUS_BOOTED:
	case IHK_OS_STATUS_READY:
		ret = IHK_STATUS_BOOTING;
		break;
	case IHK_OS_STATUS_RUNNING:
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

static int open_namespace(pid_t pid, int namespaces)
{
	int ret = 0;
	char nspath[PATH_MAX];
	struct namespace_file *nsfile;

	for (nsfile = namespace_files; nsfile->nstype; nsfile++) {
		if (!(nsfile->nstype & namespaces)) {
			continue;
		}

		if (nsfile->fd != -1) {
			close(nsfile->fd);
		}

		sprintf(nspath, "/proc/%d/%s", pid, nsfile->name);
		dprintf("%s: nspath=%s\n", __func__, nspath);

		if ((nsfile->fd = open(nspath, O_RDONLY)) == -1) {
			eprintf("%s: error: open %s: %s\n",
				__func__, nspath, strerror(errno));
			ret = -errno;
			goto out;
		}
	}
 out:
	return ret;
}

static void close_namespace(void)
{
	struct namespace_file *nsfile;

	for (nsfile = namespace_files; nsfile->nstype; nsfile++) {
		if (nsfile->fd != -1) {
			close(nsfile->fd);
			nsfile->fd = -1;
		}
	}
}

static int enter_namespace(int namespaces)
{
	int ret = 0;
	struct namespace_file *nsfile;

	for (nsfile = namespace_files; nsfile->nstype; nsfile++) {
		if (!(nsfile->nstype & namespaces)) {
			continue;
		}

		if ((ret = setns(nsfile->fd, nsfile->nstype))) {
			eprintf("%s: error: setns: %s\n",
				__func__, strerror(errno));
			ret = -errno;
			goto out;
		}
	}
 out:
	return ret;
}

/*
 *  Create /proc and /sys file system in the specified namespace
 *  index:	OS index
 *  nspid:	pid to refer namespace entries in /proc/[nspid]/ns.
 *		Specifying 0 avoids namespace reassociation.
 *  namespaces: Set of namespaces using the format of clone flag,
 *		i.e. CLONE_NEWNS, CLONE_NEWPID etc.
 */
int ihk_os_create_pseudofs(int index, pid_t nspid, int namespaces)
{
	int ret, status;
	uid_t euid;
	int fd = -1;
	pid_t pid;

	/* Check if index is valid */
	if ((fd = ihklib_os_open(index)) < 0) {
		eprintf("%s: error: ihklib_os_open\n",
			__func__);
		ret = fd;
		goto out;
	}

	if ((euid = geteuid())) {
		eprintf("%s: only root is allowed to call this function\n",
			__func__);
		ret = -EPERM;
		goto out;
	}

	if (!(pid = fork())) {
		if (nspid) {
			if ((ret = open_namespace(nspid, namespaces))) {
				eprintf("%s: error: open_namespace failed\n",
					__func__);
				goto out_child;
			}

			if ((ret = enter_namespace(namespaces))) {
				eprintf("%s: error: enter_namespace failed\n",
					__func__);
				goto out_child;
			}
		}

		status = system("/bin/bash " SBINDIR "/mcoverlay-create.sh");

		if (status == -1) {
			eprintf("%s: error: system: %s\n",
				__func__, strerror(errno));
			ret = -errno;
			goto out_child;
		}

		if (WEXITSTATUS(status) != 0) {
			signed char status_signed_char = WEXITSTATUS(status);

			eprintf("%s: error: mcoverlay-create.sh: %s\n",
				__func__, strerror(-status_signed_char));
			ret = status_signed_char;
			goto out_child;
		}

		ret = 0;
out_child:
		if (nspid) {
			close_namespace();
		}

		exit(ret);
	}

	pid = waitpid(pid, &status, 0);

	if (pid == -1) {
		eprintf("%s: error: waitpid: %s\n",
			__func__, strerror(errno));
		ret = -errno;
		goto out;
	}

	if (!WIFEXITED(status)) {
		eprintf("%s: error: child didn't terminated normally\n",
			__func__);
		ret = -EINVAL;
		goto out;
	}

	if (WEXITSTATUS(status)) {
		signed char status_signed_char = WEXITSTATUS(status);

		eprintf("%s: error: child: %s\n",
			__func__, strerror(-status_signed_char));
		ret = status_signed_char;
		goto out;
	}

	ret = 0;
 out:
	if (fd != -1) {
		close(fd);
	}

	return ret;
}

/*
 *  Destroy /proc and /sys file system in the specified namespace
 *  index:	OS index
 *  nspid:	pid to refer namespace entries in /proc/[nspid]/ns.
 *		Specifying 0 avoids namespace reassociation.
 *  namespaces: Set of namespaces using the format of clone flag,
 *		i.e. CLONE_NEWNS, CLONE_NEWPID etc.
 */
int ihk_os_destroy_pseudofs(int index, pid_t nspid, int namespaces)
{
	int ret, status;
	uid_t euid;
	int fd = -1;
	pid_t pid;

	/*
	 * Don't check the index because this function could be called
	 * after OS instance has been destroyed
	 */

	if ((euid = geteuid())) {
		eprintf("%s: only root is allowed to call this function\n",
			__func__);
		ret = -EPERM;
		goto out;
	}

	/* Join the mount name space and mount */
	if (!(pid = fork())) {
		if (nspid) {
			if ((ret = open_namespace(nspid, namespaces))) {
				eprintf("%s: error: open_namespace failed\n",
					__func__);
				goto out_child;
			}

			if ((ret = enter_namespace(namespaces))) {
				eprintf("%s: error: enter_namespace failed\n",
					__func__);
				goto out_child;
			}
		}

		status = system("/bin/bash " SBINDIR "/mcoverlay-destroy.sh");

		if (status == -1) {
			eprintf("%s: error: system: %s\n",
				__func__, strerror(errno));
			ret = -errno;
			goto out_child;
		}

		if (WEXITSTATUS(status) != 0) {
			signed char status_signed_char = WEXITSTATUS(status);

			eprintf("%s: error: mcoverlay-destroy.sh: %s\n",
				__func__, strerror(-status_signed_char));
			ret = status_signed_char;
			goto out_child;
		}

		ret = 0;
out_child:
		if (nspid) {
			close_namespace();
		}
		exit(ret);
	}

	pid = waitpid(pid, &status, 0);

	if (pid == -1) {
		eprintf("%s: error: waitpid failed: %s\n",
			__func__, strerror(errno));
		ret = -errno;
		goto out;
	}

	if (!WIFEXITED(status)) {
		eprintf("%s: error: child didn't terminated normally\n",
			__func__);
		ret = -EINVAL;
		goto out;
	}

	if (WEXITSTATUS(status)) {
		signed char status_signed_char = WEXITSTATUS(status);

		eprintf("%s: error: child: %s\n",
			__func__, strerror(-status_signed_char));
		ret = status_signed_char;
		goto out;
	}

	ret = 0;
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

	if ((fd = ihklib_os_open(index)) < 0) {
		eprintf("%s: error: ihklib_os_open\n",
			__func__);
		ret = fd;
		goto out;
	}

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

	if ((fd = ihklib_os_open(index)) < 0) {
		eprintf("%s: error: ihklib_os_open\n",
			__func__);
		ret = fd;
		goto out;
	}

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

	if ((fd = ihklib_os_open(index)) < 0) {
		eprintf("%s: error: ihklib_os_open\n",
			__func__);
		ret = fd;
		goto out;
	}

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

	if ((fd = ihklib_os_open(index)) < 0) {
		eprintf("%s: error: ihklib_os_open\n",
			__func__);
		ret = fd;
		goto out;
	}

	ret_ioctl = ioctl(fd, IHK_OS_GET_NUM_NUMA_NODES);
	CHKANDJUMP(ret_ioctl < 0, -errno, "ioctl failed\n");

	ret = ret_ioctl;
 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}

static int get_meminfo_path(char *path, int os_index, int node)
{
	return snprintf(path, PATH_MAX,
			"/sys/devices/virtual/mcos/mcos%d/"
			"sys/devices/system/node/node%d/meminfo",
			os_index, node);
}

int ihklib_os_query_mem_sysfs(int index, char *result, ssize_t sz_result,
			      const char *type)
{
	int ret;
	int node = 0;
	char path[PATH_MAX];
	int len = 0;
	struct stat sb;
	FILE *fp = NULL;

	memset(result, 0, sz_result);

	get_meminfo_path(path, index, node);

	while (stat(path, &sb) != -1) {
		unsigned long free_kb = 0;
		char *line = NULL;
		size_t line_len;

		fp = fopen(path, "r");
		CHKANDJUMP(!fp, -1, "error: opening %s\n", path);

		while (getline(&line, &line_len, fp) != -1) {
			int scan_node;
			char scanfmt[1024];
			int scanfmt_len;

			scanfmt_len = snprintf(scanfmt, sizeof(scanfmt),
					       "Node %%d %s:%%16lu kB",
					       type);
			if (scanfmt_len >= sizeof(scanfmt)) {
				eprintf("%s: error: type string (%s) is too long\n",
					__func__, type);
				ret = -1;
				goto out;
			}

			if (sscanf(line, scanfmt,
				   &scan_node, &free_kb) == 2) {
				if (node > 0)
					len += snprintf(&result[len],
							sz_result - len, ",");

				len += snprintf(&result[len], sz_result - len,
						"%lu@%d",
						free_kb * 1024, node);
			}

			free(line);
			line = NULL;
		}

		fclose(fp);
		fp = NULL;

		++node;
		get_meminfo_path(path, index, node);
	}

	CHKANDJUMP(len == 0, -1, "%s not found\n", type);

	ret = 0;
out:
	if (fp) {
		fclose(fp);
	}
	return ret;
}

static int ihklib_os_query_mem(int index, unsigned long *result,
		 int num_numa_nodes, enum ihklib_os_query_mem_type type)
{
	int i, ret;
	char result_str[16 * IHK_MAX_NUM_NUMA_NODES];
	struct ihk_mem_chunk mem_chunks[IHK_MAX_NUM_NUMA_NODES];
	int num_mem_chunks = num_numa_nodes;
	int fd = -1;

	if ((fd = ihklib_os_open(index)) < 0) {
		eprintf("%s: error: ihklib_os_open\n",
			__func__);
		ret = fd;
		goto out;
	}

	ret = ihklib_os_query_mem_sysfs(index, result_str,
					sizeof(result_str),
					ihklib_os_query_mem_type_str[type]);
	CHKANDJUMP(ret != 0, -EINVAL,
		   "ihklib_os_query_total_mem failed\n");

	memset(mem_chunks, 0, sizeof(mem_chunks));
	mem_str2array(result_str, &num_mem_chunks, mem_chunks);

	CHKANDJUMP(num_mem_chunks != num_numa_nodes, -EINVAL,
		   "actual number of NUMA nodes (%d) is different than requested (%d)\n",
		   num_mem_chunks, num_numa_nodes);

	for (i = 0; i < num_mem_chunks; i++) {
		CHKANDJUMP(mem_chunks[i].numa_node_number >= num_numa_nodes ||
			   mem_chunks[i].numa_node_number < 0, -EINVAL,
			   "NUMA node number out of range\n");
		result[mem_chunks[i].numa_node_number] = mem_chunks[i].size;
	}

	ret = 0;
 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}

int ihk_os_query_total_mem(int index, unsigned long *result,
			   int num_numa_nodes)
{
	return ihklib_os_query_mem(index, result, num_numa_nodes,
				   IHKLIB_OS_QUERY_MEM_TOTAL);
}

int ihk_os_query_free_mem(int index, unsigned long *result,
		      int num_numa_nodes)
{
	return ihklib_os_query_mem(index, result, num_numa_nodes,
				   IHKLIB_OS_QUERY_MEM_FREE);
}

int ihk_os_get_num_pagesizes(int index)
{
	int ret = 0;
	int fd = -1;

	if ((fd = ihklib_os_open(index)) < 0) {
		eprintf("%s: error: ihklib_os_open\n",
			__func__);
		ret = fd;
		goto out;
	}

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

	if ((fd = ihklib_os_open(index)) < 0) {
		eprintf("%s: error: ihklib_os_open\n",
			__func__);
		ret = fd;
		goto out;
	}

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

	if ((fd = ihklib_os_open(index)) < 0) {
		eprintf("%s: error: ihklib_os_open\n",
			__func__);
		ret = fd;
		goto out;
	}

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

	if ((fd = ihklib_os_open(index)) < 0) {
		eprintf("%s: error: ihklib_os_open\n",
			__func__);
		ret = fd;
		goto out;
	}

	ret_ioctl = ioctl(fd, IHK_OS_AUX_PERF_NUM, n);
	CHKANDJUMP(ret_ioctl != 0, -errno, "ioctl failed\n");

	ret_ioctl = ioctl(fd, IHK_OS_AUX_PERF_SET, attr);
	CHKANDJUMP(ret_ioctl < 0, -errno, "ioctl failed\n");

	ret = ret_ioctl;
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

	if ((fd = ihklib_os_open(index)) < 0) {
		eprintf("%s: error: ihklib_os_open\n",
			__func__);
		ret = fd;
		goto out;
	}

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
		ret = -EINVAL;
		goto out;
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

	if ((fd = ihklib_os_open(index)) < 0) {
		eprintf("%s: error: ihklib_os_open\n",
			__func__);
		ret = fd;
		goto out;
	}

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
			if ((fd = ihklib_os_open(index)) < 0) {
				eprintf("%s: error: ihklib_os_open\n",
					__func__);
				ret = fd;
				goto out;
			}

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
			if ((fd = ihklib_os_open(index)) < 0) {
				eprintf("%s: error: ihklib_os_open\n",
					__func__);
				ret = fd;
				goto out;
			}

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

	dprintf("%s: index=%d,dump_file=%s,dump_level=%d,interactive=%d\n",
		__func__, index, dump_file, dump_level, interactive);

	if ((osfd = ihklib_os_open(index)) < 0) {
		eprintf("%s: error: ihklib_os_open\n",
			__func__);
		ret = osfd;
		goto out;
	}

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
	dprintf("%s: nr chunks: %d\n",
		__func__, mem_chunks->nr_chunks);
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
	if (abfd) {
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

/*
 * Messages with level below or equal to loglevel
 * are printed out
 */
int ihk_set_loglevel(enum IHKLIB_LOGLEVEL level)
{
	loglevel = level;
	return 0;
}
