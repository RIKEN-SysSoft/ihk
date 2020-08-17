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

#include "driver/ihk_host_user.h"
#include "ihk/ihklib.h"
#include "user/ihklib_private.h"
#include "user/okng_user.h"
#include "branch_info.h"
#include "arr_utils.h"

#include "blackbox/include/cpu.h"
#include "blackbox/include/mem.h"

int __argc;
char **__argv;

int loglevel = IHKLIB_LOGLEVEL_ERR;

//#define DEBUG

#ifdef DEBUG
#define dprintf(fmt, args...) do {  \
  printf(fmt, ##args);    \
} while (0)

#define dprintk(fmt, args...) do {          \
  char contents[4096 - 256];          \
  int fd;                \
  ssize_t len;              \
  ssize_t offset = 0;            \
  if (geteuid()) {            \
    break;              \
  }                \
  sprintf(contents, fmt, ##args);          \
  fd = open("/dev/kmsg", O_WRONLY);        \
  len = strlen(contents) + 1;          \
  while (offset < len) {            \
    offset += write(fd, contents + offset, len - offset);  \
  }                \
  close(fd);              \
} while (0)

#else
#define dprintf(fmt, args...) do {  } while (0)
#define dprintk(fmt, args...) do {  } while (0)
#endif

#define eprintf(fmt, args...) do {    \
  if (loglevel >= IHKLIB_LOGLEVEL_ERR) {  \
    fprintf(stderr, fmt, ##args);  \
  }          \
} while (0)


#define PHYSMEM_NAME_SIZE 32

#define CHKANDJUMP(cond, err, fmt, args...) do {  \
  if (cond) {          \
    ret = err;        \
    dprintf(fmt, ##args);      \
    goto out;        \
  }            \
} while(0)


struct namespace_file namespace_files[] = {
  { .nstype = CLONE_NEWUSER,  .name = "ns/user", .fd = -1 },
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,6,0)
  { .nstype = CLONE_NEWCGROUP,  .name = "ns/cgroup", .fd = -1 },
#endif
  { .nstype = CLONE_NEWIPC,  .name = "ns/ipc", .fd = -1 },
  { .nstype = CLONE_NEWUTS,  .name = "ns/uts", .fd = -1 },
  { .nstype = CLONE_NEWNET,  .name = "ns/net", .fd = -1 },
  { .nstype = CLONE_NEWPID,  .name = "ns/pid", .fd = -1 },
  { .nstype = CLONE_NEWNS,  .name = "ns/mnt", .fd = -1 },
  { .nstype = 0, .name = NULL, .fd = -1 }
};

struct ihklib_reserve_mem_conf reserve_mem_conf = {
  .total = 0,
  .variance_limit = 0,
  .min_chunk_size = PAGE_SIZE,
  .max_size_ratio_all = 100,
  .timeout = 30,
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
    ret = -errno;
    dprintf("%s: error: allocating cpu_list\n",
      __func__);
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
    ret = -errno;
    dprintf("%s: error: allocating ikc_list\n",
      __func__);
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


#define IHK_SMP_MEM_ALL  (-1UL)
static size_t ihk_memparse(char *token)
{
  size_t ret;
  char *endp = token + strlen(token) - 1;

  /* "all" or "ALL" indicates best effort allocation */
  if (!strcmp("all", token) || !strcmp("ALL", token)) {
    ret = IHK_SMP_MEM_ALL;
    goto out;
  }

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

out:
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
    ret = -errno;
    dprintf("%s: error: allocating mem_list\n",
      __func__);
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

static int get_test_mode()
{
  char *val = getenv(IHKLIB_TEST_MODE_ENV_NAME);
  if (val == NULL) return TEST_NONE;

  return atoi(val);
}

static int ihklib_device_readable_orig(int index)
{
  int ret;
  char fn[PATH_MAX];

  sprintf(fn, "/dev/mcd%d", index);
  ret = access(fn, R_OK);
  if (ret) {
    ret = -errno;
    dprintf("%s: error: access: path: %s, errno: %d\n",
      __func__, fn, -ret);
    goto out;
  }

  ret = 0;
 out:
  return ret;
}

static int ihklib_device_readable(int index)
{
  if (get_test_mode() != TEST_IHKLIB_DEVICE_READABLE)
    return ihklib_device_readable_orig(index);

  int ret;
  char fn[PATH_MAX];

  unsigned long ivec = 0;
  unsigned long total_branch = 2;

  branch_info_t b_infos[] = {
    { -1, "access error" },
    { 0, "main case" }
  };

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    sprintf(fn, "/dev/mcd%d", index);
    ret = access(fn, R_OK);
    if (ivec == 0 || ret) {
      ret = -1;
      if (ivec == 0) goto out;
      ret = -errno;
      dprintf("%s: error: access: path: %s, errno: %d\n",
        __func__, fn, -ret);
      goto err;
    }

   out:
    BRANCH_RET_CHK(ret, b_infos[ivec].expected);
  }

  ret = 0;

 err:
  return ret;
}

int ihklib_device_open_orig(int index)
{
  int ret;
  char fn[PATH_MAX];

  ret = ihklib_device_readable(index);
  if (ret) {
    dprintf("%s: error: ihklib_device_readable returned %d\n",
      __func__, ret);
    goto out;
  }

  sprintf(fn, "/dev/mcd%d", index);
  if ((ret = open(fn, O_RDONLY)) == -1) {
    ret = -errno;
    dprintf("%s: error: open %s: %s\n",
      __func__, fn, strerror(-ret));
    goto out;
  }

 out:
  return ret;
}

int ihklib_device_open(int index)
{
  if (get_test_mode() != TEST_IHKLIB_DEVICE_OPEN)
    return ihklib_device_open_orig(index);

  int ret;
  char fn[PATH_MAX];

  unsigned long ivec = 0;
  unsigned long total_branch = 3;
  int fd = -1;

  branch_info_t b_infos[] = {
    { -EINVAL, "not readable" },
    { -ENOENT, "open error" },
    { 0, "main case" }
  };

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    int should_quit = 0;
    ret = ihklib_device_readable(index);
    if (ivec == 0 || ret) {
      ret = -EINVAL;
      if (ivec != 0) {
        ret = -errno;
        dprintf("%s: error: ihklib_device_readable returned %d\n",
          __func__, ret);
        should_quit = 1;
      }
      goto out;
    }

    sprintf(fn, "/dev/mcd%d", index);
    ret = open(fn, O_RDONLY);
    fd = ret;
    if (ivec == 1 || ret == -1) {
      ret = -ENOENT;
      if (ivec != 1) {
        ret = -errno;
        dprintf("%s: error: open %s: %s\n", __func__, fn, strerror(-ret));
        should_quit = 1;
      }
      goto out;
    }

    // ivec == 2
    if (ret > 0) ret = 0;

   out:
    BRANCH_RET_CHK(ret, b_infos[ivec].expected);

    // Try to query opened device
    if (ivec == total_branch-1 && fd >= 0) {
      ret = ioctl(fd, IHK_DEVICE_GET_NUM_CPUS);
      OKNG(ret >= 0, "query opened device, errno: %d\n", -errno);
    }

    if (ivec < total_branch-1 && fd >= 0) {
      close(fd); fd = -1;
    }

    if (should_quit) return ret;
  }

  return (ret < 0)? -errno : fd;

 err:
  if (fd >= 0) close(fd);
  return -errno;
}

int ihk_reserve_cpu_orig(int index, int* cpus, int num_cpus)
{
  int ret;
  struct ihk_ioctl_cpu_desc req = { 0 };
  int fd = -1;

  dprintk("%s: enter\n", __func__);

  ret = ihklib_device_readable(index);
  if (ret) {
    goto out;
  }

  if (num_cpus < 0 || num_cpus > IHK_MAX_NUM_CPUS) {
    dprintf("%s: invalid number of cpus (%d)\n",
      __func__, num_cpus);
    ret = -EINVAL;
    goto out;
  }

  if (num_cpus != 0 && cpus == NULL) {
    ret = -EFAULT;
    goto out;
  }

  if (num_cpus == 0) {
    ret = 0;
    goto out;
  }

  req.cpus = cpus;
  req.num_cpus = num_cpus;

  if ((fd = ihklib_device_open(index)) < 0) {
    dprintf("%s: error: ihklib_device_open returned %d\n",
      __func__, fd);
    ret = fd;
    goto out;
  }

  ret = ioctl(fd, IHK_DEVICE_RESERVE_CPU, &req);
  if (ret) {
    ret = -errno;
    dprintf("%s: IHK_DEVICE_RESERVE_CPU returned %d\n",
      __func__, -ret);
    goto out;
  }

 out:
  if (fd != -1) {
    close(fd);
  }
  return ret;
}

int ihk_reserve_cpu(int index, int* cpus, int num_cpus)
{
  if (get_test_mode() != TEST_IHK_RESERVE_CPU)
    return ihk_reserve_cpu_orig(index, cpus, num_cpus);

  int ret;
  int fd = -1;

  unsigned long ivec = 0;
  unsigned long total_branch = 7;

  branch_info_t b_infos[] = {
    { -ENOENT, "not readable" },
    { -EINVAL, "invalid number of cpus" },
    { -EINVAL, "cpu_list is null" },
    { -EINVAL, "num_cpus is zero" },
    { -ENOENT, "device is not opened" },
    { -ENOENT, "cannot reserve cpus" },
    { 0, "main case" }
  };

  int ncpus_before = get_nprocs();
  struct cpus cpus_before = { 0 };
  ret = cpus_ls(&cpus_before);
  if (ret) return ret;

  dprintk("%s: enter\n", __func__);
  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    struct ihk_ioctl_cpu_desc req = { 0 };
    int should_quit = 0;
    int fail = 0;

    ret = ihklib_device_readable(index);
    if (ivec == 0 || ret) {
      ret = -ENOENT;
      if (ivec != 0) should_quit = 1;
      goto out;
    }

    if (ivec == 1 || (num_cpus < 0 || num_cpus > IHK_MAX_NUM_CPUS)) {
      ret = -EINVAL;
      if (ivec != 1) {
        dprintf("%s: invalid number of cpus (%d)\n", __func__, num_cpus);
        should_quit = 1;
      }
      goto out;
    }

    if (ivec == 2 || (num_cpus != 0 && cpus == NULL)) {
      //ret = -EFAULT;
      ret = -EINVAL;
      if (ivec != 2) should_quit = 1;
      goto out;
    }

    if (ivec == 3 || num_cpus == 0) {
      //ret = 0;
      ret = -EINVAL;
      if (ivec != 3) should_quit = 1;
      goto out;
    }

    req.cpus = cpus;
    req.num_cpus = num_cpus;

    if (ivec != 4)
      fd = ihklib_device_open(index);
    if (ivec == 4 || fd < 0) {
      ret = -ENOENT;
      if (ivec != 4) {
        dprintf("%s: error: ihklib_device_open returned %d\n", __func__, fd);
        should_quit = 1;
      }
      goto out;
    }

    if (ivec != 5) ret = ioctl(fd, IHK_DEVICE_RESERVE_CPU, &req);
    if (ivec == 5 || ret) {
      ret = -ENOENT;
      if (ivec != 5) {
        ret = -errno;
        dprintf("%s: IHK_DEVICE_RESERVE_CPU returned %d\n", __func__, -ret);
        should_quit = 1;
      }
      goto out;
    }

   out:
    if (fd != -1) {
      close(fd); fd = -1;
    }

    BRANCH_RET_CHK(ret, b_infos[ivec].expected);
    int ncpus_after = get_nprocs();
    struct cpus cpus_after = { 0 };
    fail = cpus_ls(&cpus_after);
    if (fail) return fail;

    // Check reserved cpus
    if (ivec == total_branch-1) {
      if ((ncpus_before - ncpus_after) == num_cpus) {
        // reserved cpus should not intersect remain Linux cpus
        fail = arr_is_intersect(cpus, num_cpus, cpus_after.cpus, cpus_after.ncpus);
        OKNG(!fail, "list of reserved cpus doesn't intersect with list of online cpus\n");

        int *cpus_merge = calloc(ncpus_before, sizeof(int));
        if (!cpus_merge) return -ENOMEM;
        int i;
        for (i = 0; i < ncpus_after; i++)
          cpus_merge[i] = cpus_after.cpus[i];
        for (i = 0; i < num_cpus; i++)
          cpus_merge[i+ncpus_after] = cpus[i];
        arr_sort(cpus_merge, ncpus_before);
        // compare
        for (i = 0; i < ncpus_before; i++) {
          if (cpus_merge[i] != cpus_before.cpus[i]) {
            fail = 1; break;
          }
        }
        free(cpus_merge);
        OKNG(!fail, "checking list of online cpus\n");
      } else {
        OKNG(0, "invalid number of reserved cpus. before: %d, after: %d\n", ncpus_before, ncpus_after);
      }
    } else {  // other branches should not affect the system
      OKNG(ncpus_after == ncpus_before, "ncpus_before: %d, ncpus_after: %d\n", ncpus_before, ncpus_after);
    }

    if (should_quit) return ret;
  }

  return ret;

 err:
  return -ENOENT;
}

int ihk_get_num_reserved_cpus_orig(int index)
{
  int ret;
  int fd = -1;

  dprintk("%s: enter\n", __func__);
  if ((fd = ihklib_device_open(index)) < 0) {
    dprintf("%s: ihklib_device_open returned %d\n",
      __func__, fd);
    ret = fd;
    goto out;
  }

  ret = ioctl(fd, IHK_DEVICE_GET_NUM_CPUS);
  if (ret < 0) {
    ret = -errno;
    dprintf("%s: IHK_DEVICE_GET_NUM_CPUS returned %d\n",
      __func__, -ret);
    goto out;
  }

 out:
  if (fd != -1) {
    close(fd);
  }
  return ret;
}

int ihk_get_num_reserved_cpus(int index)
{
  if (get_test_mode() != TEST_IHK_GET_NUM_RESERVED_CPUS)
    return ihk_get_num_reserved_cpus_orig(index);

  int ret;
  int fd = -1;

  dprintk("%s: enter\n", __func__);

  unsigned long ivec = 0;
  unsigned long total_branch = 3;
  int should_quit = 0;

  branch_info_t b_infos[] = {
    { -ENOENT, "cannot open device" },
    { -EINVAL, "cannot get number of reserved cpus" },
    { 0, "main case" }
  };

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    if (ivec != 0)
      fd = ihklib_device_open(index);
    if (ivec == 0 || fd < 0) {
      ret = -ENOENT;
      if (ivec != 0) {
        dprintf("%s: ihklib_device_open returned %d\n",
          __func__, fd);
        should_quit = 1;
        ret = fd;
      }
      goto out;
    }

    ret = ioctl(fd, IHK_DEVICE_GET_NUM_CPUS);
    if (ivec == 1 || ret < 0) {
      ret = -EINVAL;
      if (ivec != 1) {
        ret = -errno;
        dprintf("%s: IHK_DEVICE_GET_NUM_CPUS returned %d\n",
          __func__, -ret);
        should_quit = 1;
      }
      goto out;
    }

    if (ret > 0) ret = 0;  // for main case

   out:
    if (fd != -1) {
      close(fd); fd = -1;
    }

    BRANCH_RET_CHK(ret, b_infos[ivec].expected);
  }

  return 0;
 err:
  return -EINVAL;
}

int ihk_query_cpu_orig(int index, int *cpus, int num_cpus)
{
  int ret;
  struct ihk_ioctl_cpu_desc req = { 0 };
  int fd = -1;

  dprintk("%s: enter\n", __func__);

  ret = ihklib_device_readable(index);
  if (ret) {
    goto out;
  }

  if (num_cpus < 0 || num_cpus > IHK_MAX_NUM_CPUS) {
    dprintf("%s: invalid number of cpus (%d)\n",
      __func__, num_cpus);
    ret = -EINVAL;
    goto out;
  }

  if ((fd = ihklib_device_open(index)) < 0) {
    dprintf("%s: ihklib_device_open returned %d\n",
      __func__, fd);
    ret = fd;
    goto out;
  }

  if ((ret = ioctl(fd, IHK_DEVICE_GET_NUM_CPUS)) < 0) {
    ret = -errno;
    dprintf("%s: IHK_DEVICE_GET_NUM_CPUS returned %d\n",
      __func__, -ret);
    goto out;
  }

  if (ret != num_cpus) {
    dprintf("%s: error: actual # of cpus (%d) != requested (%d)\n",
      __func__, ret, num_cpus);
    ret = -EINVAL;
    goto out;
  }

  req.cpus = cpus;
  req.num_cpus = num_cpus;

  if ((ret = ioctl(fd, IHK_DEVICE_QUERY_CPU, &req))) {
    ret = -errno;
    dprintf("%s: error: IHK_DEVICE_QUERY_CPU returned %d\n",
      __func__, -ret);
    goto out;
  }

 out:
  if (fd != -1) {
    close(fd);
  }
  return ret;
}

int ihk_query_cpu(int index, int *cpus, int num_cpus)
{
  if (get_test_mode() != TEST_IHK_QUERY_CPU)
    return ihk_query_cpu_orig(index, cpus, num_cpus);

  int ret;
  int fd = -1;
  unsigned long ivec = 0;
  unsigned long total_branch = 7;
  int should_quit = 0;

  branch_info_t b_infos[] = {
    { -ENOENT, "not readable" },
    { -EINVAL, "invalid number of cpus" },
    { -EINVAL, "cannot open device" },
    { -EINVAL, "cannot get num cpus" },
    { -EINVAL, "cannot match #cpus" },
    { -EINVAL, "query cpu fail" },
    { 0, "main case" }
  };

  dprintk("%s: enter\n", __func__);

  int ncpus_before = get_nprocs();
  struct cpus cpus_before = { 0 };
  ret = cpus_ls(&cpus_before);
  if (ret) return ret;

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    struct ihk_ioctl_cpu_desc req = { 0 };

    ret = ihklib_device_readable(index);
    if (ivec == 0 || ret) {
      ret = -ENOENT;
      if (ivec != 0) should_quit = 1;
      goto out;
    }

    if (ivec == 1 || (cpus == NULL || num_cpus < 0 || num_cpus > IHK_MAX_NUM_CPUS)) {
      if (ivec != 1) {
        dprintf("%s: invalid number of cpus (%d)\n",
          __func__, num_cpus);
        should_quit = 1;
      }
      ret = -EINVAL;
      goto out;
    }

    if (ivec != 2)
      fd = ihklib_device_open(index);
    if (ivec == 2 || fd < 0) {
      if (ivec != 2) {
        dprintf("%s: ihklib_device_open returned %d\n",
          __func__, fd);
        should_quit = 1;
      }
      ret = -EINVAL;
      goto out;
    }

    ret = ioctl(fd, IHK_DEVICE_GET_NUM_CPUS);
    if (ivec == 3 || ret < 0) {
      if (ivec != 3) {
        ret = -errno;
        dprintf("%s: IHK_DEVICE_GET_NUM_CPUS returned %d\n",
          __func__, -ret);
        should_quit = 1;
      }
      ret = -EINVAL;
      goto out;
    }

    if (ivec == 4 || ret != num_cpus) {
      if (ivec != 4) {
        dprintf("%s: error: actual # of cpus (%d) != requested (%d)\n",
          __func__, ret, num_cpus);
        should_quit = 1;
      }
      ret = -EINVAL;
      goto out;
    }

    req.cpus = cpus;
    req.num_cpus = num_cpus;

    ret = ioctl(fd, IHK_DEVICE_QUERY_CPU, &req);
    if (ivec == 5 || ret) {
      if (ivec != 5) {
        ret = -errno;
        dprintf("%s: error: IHK_DEVICE_QUERY_CPU returned %d\n",
          __func__, -ret);
        should_quit = 1;
      }
      ret = -EINVAL;
      goto out;
    }

   out:
    if (fd >= 0) {
      close(fd); fd = -1;
    }

    BRANCH_RET_CHK(ret, b_infos[ivec].expected);

    int ncpus_after = get_nprocs();
    struct cpus cpus_after = { 0 };
    int stat = cpus_ls(&cpus_after);
    if (stat) return stat;
    /* state must be unchanged */
    OKNG(ncpus_before == ncpus_after, "ncpus_before: %d, ncpus_after: %d\n",
      ncpus_before, ncpus_after);
    OKNG(cpus_compare(&cpus_after, &cpus_before) == 0,
      "list of online cpus must be unchanged\n");

    if (ivec == total_branch-1) {
      int i, j;
      int fail = 0;
      // reserved cpus should not intersect remain Linux cpus
      for (i = 0; i < num_cpus; ++i) {
        for (j = 0; j < cpus_after.ncpus; ++j) {
          if (cpus[i] == cpus_after.cpus[j] ) {
            fail = 1;
            break;
          }
        }
        if (fail) break;
      }
      OKNG(!fail, "list of reserved cpus\n");
    }

    if (should_quit) return ret;
  }

  return ret;
 err:
  return -1;
}

int ihk_release_cpu_orig(int index, int* cpus, int num_cpus)
{
  int ret;
  struct ihk_ioctl_cpu_desc req = { 0 };
  int fd = -1;

  dprintk("%s: enter\n", __func__);

  ret = ihklib_device_readable(index);
  if (ret) {
    goto out;
  }

  if (num_cpus < 0 || num_cpus > IHK_MAX_NUM_CPUS) {
    dprintf("%s: invalid num_cpus: %d\n",
      __func__, num_cpus);
    ret = -EINVAL;
    goto out;
  }

  if (num_cpus != 0 && cpus == NULL) {
    ret = -EFAULT;
    goto out;
  }

  if (num_cpus == 0) {
    ret = 0;
    goto out;
  }

  req.cpus = cpus;
  req.num_cpus = num_cpus;

  if ((fd = ihklib_device_open(index)) < 0) {
    dprintf("%s: error: ihklib_device_open\n",
      __func__);
    ret = fd;
    goto out;
  }

  ret = ioctl(fd, IHK_DEVICE_RELEASE_CPU, &req);
  if (ret) {
    ret = -errno;
    dprintf("%s: IHK_DEVICE_RELEASE_CPU returned %d\n",
      __func__, -ret);
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
  if (get_test_mode() != TEST_IHK_RELEASE_CPU)
    return ihk_release_cpu_orig(index, cpus, num_cpus);

  dprintk("%s: enter\n", __func__);

  unsigned long ivec = 0;
  unsigned long total_branch = 7;

  branch_info_t b_infos[] = {
    { -ENOENT, "branch 1" },
    { -EINVAL, "branch 2" },
    { -EINVAL, "branch 3" },
    { 0,       "branch 4" },
    { -EINVAL, "branch 5" },
    { -EINVAL, "branch 6" },
    { 0,       "main case" }
  };

  int ret = 0;
  int ncpus_before = get_nprocs();
  struct cpus cpus_before = { 0 };
  ret = cpus_ls(&cpus_before);
  if (ret) return ret;

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    struct ihk_ioctl_cpu_desc req = { 0 };
    int fd = -1;
    int should_quit = 0;

    ret = ihklib_device_readable(index);
    if (ivec == 0 || ret) {
      ret = -ENOENT;
      if (ivec != 0) should_quit = 1;
      goto out;
    }

    if (ivec == 1 || (num_cpus < 0 || num_cpus > IHK_MAX_NUM_CPUS)) {
      if (ivec != 1) {
        dprintf("%s: invalid num_cpus: %d\n",
          __func__, num_cpus);
        should_quit = 1;
      }
      ret = -EINVAL;
      goto out;
    }

    if (ivec == 2 || (num_cpus != 0 && cpus == NULL)) {
      ret = -EINVAL;
      if (ivec != 2) should_quit = 1;
      goto out;
    }

    if (ivec == 3 || num_cpus == 0) {
      ret = 0;
      if (ivec != 3) should_quit = 1;
      goto out;
    }

    req.cpus = cpus;
    req.num_cpus = num_cpus;

    if (ivec != 4)
      fd = ihklib_device_open(index);
    if (ivec == 4 || fd < 0) {
      ret = -EINVAL;
      if (ivec != 4) {
        dprintf("%s: error: ihklib_device_open\n",
          __func__);
        ret = fd;
        should_quit = 1;
      }
      goto out;
    }

    if (ivec != 5)
      ret = ioctl(fd, IHK_DEVICE_RELEASE_CPU, &req);
    if (ivec == 5 || ret) {
      ret = -EINVAL;
      if (ivec != 5) {
        ret = -errno;
        dprintf("%s: IHK_DEVICE_RELEASE_CPU returned %d\n",
          __func__, -ret);
        should_quit = 1;
      }
      goto out;
    }

   out:
    if (fd != -1) {
      close(fd); fd = -1;
    }
    if (should_quit) goto err;
    should_quit = 1;

    BRANCH_RET_CHK(ret, b_infos[ivec].expected);

    /* check state */
    {
      int ncpus_after = get_nprocs();
      struct cpus cpus_after = { 0 };
      int stat = cpus_ls(&cpus_after);
      if (stat) return stat;
      if (ivec == total_branch-1) {
        OKNG(ncpus_after == ncpus_before + num_cpus,
          "the number of online cpus must be correct\n");

        /* check cpus list */
        int *cpus_merge = calloc(ncpus_after, sizeof(int));
        if (!cpus_merge) return -ENOMEM;
        int i;
        for (i = 0; i < ncpus_before; i++)
          cpus_merge[i] = cpus_before.cpus[i];
        for (i = 0; i < num_cpus; i++)
          cpus_merge[i+ncpus_before] = cpus[i];
        arr_sort(cpus_merge, ncpus_after);
        // compare
        int fail = 0;
        for (i = 0; i < ncpus_after; i++) {
          if (cpus_merge[i] != cpus_after.cpus[i]) {
            fail = 1; break;
          }
        }
        free(cpus_merge);
        OKNG(!fail, "checking list of online cpus\n");
      } else {
        OKNG(ncpus_before == ncpus_after,
          "the number of online cpus must be unchanged\n");
        stat = cpus_compare(&cpus_after, &cpus_before);
        OKNG(stat == 0, "list of online cpus must be unchanged\n");
      }
    }

   err:
    if (should_quit) return (ret)? ret : -1;
  }

  return 0;
}

int ihk_reserve_mem_conf_orig(int index, int key, void *value)
{
  int ret;

  ret = ihklib_device_readable(index);
  if (ret) {
    goto out;
  }

  switch (key) {
  case IHK_RESERVE_MEM_TOTAL:
    reserve_mem_conf.total = 1;
    reserve_mem_conf.variance_limit = *((int *)value);
    break;
  case IHK_RESERVE_MEM_MIN_CHUNK_SIZE:
    reserve_mem_conf.min_chunk_size = *((int *)value);
    break;
  case IHK_RESERVE_MEM_MAX_SIZE_RATIO_ALL:
    reserve_mem_conf.max_size_ratio_all = *((int *)value);
    break;
  case IHK_RESERVE_MEM_TIMEOUT:
    reserve_mem_conf.timeout = *((int *)value);
    break;
  default:
    ret = -EINVAL;
    goto out;
  }

  ret = 0;
 out:
  return ret;
}

int ihk_reserve_mem_conf(int index, int key, void *value)
{
  if (get_test_mode() != TEST_IHK_RESERVE_MEM_CONF)
    return ihk_reserve_mem_conf_orig(index, key, value);

  unsigned long ivec = 0;
  unsigned long total_branch = 4;
  int should_quit = 0;

  branch_info_t b_infos[] = {
    { -ENOENT, "device not readable" },
    { -EINVAL, "invalid value" },
    { -EINVAL, "invalid key" },
    { 0, "main case" }
  };

  int ret = 0;
  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    ret = ihklib_device_readable(index);
    if (ivec == 0 || ret) {
      ret = -ENOENT;
      if (ivec != 0) should_quit = 1;
      goto out;
    }

    if (ivec == 1 || value == NULL) {
      ret = -EINVAL;
      if (ivec != 1) should_quit = 1;
      goto out;
    }

    if (ivec == 2) key = -10;  // fake invalid key

    switch (key) {
    case IHK_RESERVE_MEM_TOTAL:
      reserve_mem_conf.total = 1;
      reserve_mem_conf.variance_limit = *((int *)value);
      break;
    case IHK_RESERVE_MEM_MIN_CHUNK_SIZE:
      reserve_mem_conf.min_chunk_size = *((int *)value);
      break;
    case IHK_RESERVE_MEM_MAX_SIZE_RATIO_ALL:
      reserve_mem_conf.max_size_ratio_all = *((int *)value);
      break;
    case IHK_RESERVE_MEM_TIMEOUT:
      reserve_mem_conf.timeout = *((int *)value);
      break;
    default:
      ret = -EINVAL;
      goto out;
    }

   out:
    BRANCH_RET_CHK(ret, b_infos[ivec].expected);
    if (should_quit) return ret;
  }

  return 0;
 err:
  if (ret == 0) ret = -1;
  return ret;
}

int ihk_reserve_mem_orig(int index, struct ihk_mem_chunk *mem_chunks,
        int num_mem_chunks)
{
  int ret;
  int i;
  struct ihk_mem_req req = { 0 };
  int fd = -1;

  size_t total_requested = 0;
  int num_mem_chunks_reserved;
  struct ihk_mem_chunk *mem_chunks_reserved = NULL;
  size_t *reserved = NULL;
  int num_numa_nodes = 0;
  int num_numa_nodes_compensate = 0;
  int num_numa_nodes_release = 0;
  size_t ave_requested;
  size_t total_missing = 0, total_excess = 0;
  size_t ave_compensate;
  size_t total_missing2 = 0, total_excess2 = 0;
  size_t ave_compensate2;
  unsigned long min = (unsigned long)-1;
  unsigned long max = 0;
  unsigned long variance_limit;
  int release = 0;

  dprintk("%s: reserve_mem_conf.total=%d\n",
    __func__, reserve_mem_conf.total);

  ret = ihklib_device_readable(index);
  if (ret) {
    goto out;
  }

  if (num_mem_chunks < 0 || num_mem_chunks > IHK_MAX_NUM_MEM_CHUNKS) {
    dprintf("%s: error: invalid # of chunks (%d)\n",
      __func__, num_mem_chunks);
    ret = -EINVAL;
    goto out;
  }

  if (num_mem_chunks != 0 && mem_chunks == NULL) {
    ret = -EFAULT;
    goto out;
  }

  if (num_mem_chunks == 0) {
    ret = 0;
    goto out;
  }

  req.sizes = calloc(num_mem_chunks, sizeof(size_t));
  if (!req.sizes) {
    dprintf("%s: error: allocating req.sizes\n",
      __func__);
    ret = -ENOMEM;
    goto out;
  }

  req.numa_ids = calloc(num_mem_chunks, sizeof(int));
  if (!req.numa_ids) {
    dprintf("%s: error: allocating req.numa_ids\n",
      __func__);
    ret = -ENOMEM;
    goto out;
  }

  for (i = 0; i < num_mem_chunks; i++) {
    if (reserve_mem_conf.total) {
      req.sizes[i] = (size_t)IHK_SMP_MEM_ALL;
      total_requested += (size_t)mem_chunks[i].size;
    } else {
      req.sizes[i] = (size_t)mem_chunks[i].size;
    }
    req.numa_ids[i] = mem_chunks[i].numa_node_number;
  }
  req.num_chunks = num_mem_chunks;
  req.min_chunk_size = reserve_mem_conf.min_chunk_size;
  req.max_size_ratio_all = reserve_mem_conf.max_size_ratio_all;
  req.timeout = reserve_mem_conf.timeout;

  fd = ihklib_device_open(index);
  if (fd < 0) {
    ret = fd;
    printf("%s: ihklib_device_open returned %d\n",
           __func__, fd);
    goto out;
  }

  ret = ioctl(fd, IHK_DEVICE_RESERVE_MEM, &req);
  if (ret != 0) {
    ret = -errno;
    dprintf("%s: IHK_DEVICE_RESERVE_MEM returned %d\n",
      __func__, -ret);
    goto out;
  }

  close(fd);

  if (reserve_mem_conf.total) {
    dprintk("%s: total requested: %ld\n",
      __func__, total_requested);

    num_mem_chunks_reserved =
      ihk_get_num_reserved_mem_chunks(index);
    mem_chunks_reserved = calloc(num_mem_chunks_reserved,
            sizeof(struct ihk_mem_chunk));
    CHKANDJUMP(mem_chunks_reserved == NULL, -ENOMEM,
         "failed to allocate mem_chunks_reserved\n");

    ret = ihk_query_mem(index, mem_chunks_reserved,
            num_mem_chunks_reserved);
    CHKANDJUMP(ret, -EINVAL, "ihk_query_mem failed\n");

    reserved = calloc(IHK_MAX_NUM_NUMA_NODES, sizeof(size_t));
    CHKANDJUMP(reserved == NULL, -ENOMEM,
         "failed to allocate reserved\n");

    for (i = 0; i < num_mem_chunks_reserved; i++) {
      reserved[mem_chunks_reserved[i].numa_node_number] +=
        mem_chunks_reserved[i].size;
    }

    for (i = 0; i < IHK_MAX_NUM_NUMA_NODES; i++) {
      if (reserved[i] == 0) {
        continue;
      }
      num_numa_nodes++;
    }

/* align reserve/release amount */
#define IHKLIB_RESERVE_AMOUNT_ALIGN (1UL << 20)

    /* round up not to release too much */
    ave_requested = ((total_requested / num_numa_nodes +
          IHKLIB_RESERVE_AMOUNT_ALIGN - 1) /
         IHKLIB_RESERVE_AMOUNT_ALIGN) *
      IHKLIB_RESERVE_AMOUNT_ALIGN;
    dprintk("%s: ave requested: %ld\n",
      __func__, ave_requested);

    /* Fill below-average-of-requested nodes upto the average */
    for (i = 0; i < IHK_MAX_NUM_NUMA_NODES; i++) {
      if (reserved[i] == 0) {
        continue;
      }
      dprintk("%s: node id: %d, reserved: %ld\n",
        __func__, i, reserved[i]);
      if (reserved[i] > ave_requested) {
        num_numa_nodes_compensate++;
        total_excess += reserved[i] - ave_requested;
      } else {
        total_missing += ave_requested - reserved[i];
      }
    }

    if (total_missing > total_excess) {
      dprintf("%s: error: "
        "sum of below-ave (%ld, %ld MiB) > "
        "sum of above-ave (%ld, %ld MiB)\n",
        __func__,
        total_missing, total_missing >> 20,
        total_excess, total_excess >> 20);
      release = 1;
      ret = -ENOMEM;
      goto out;
    }

    dprintk("%s: total missing: %ld\n",
      __func__, total_missing);

    req.sizes = calloc(IHK_MAX_NUM_NUMA_NODES, sizeof(size_t));
    CHKANDJUMP(req.sizes == NULL, -ENOMEM,
         "failed to allocate torelease\n");

    req.numa_ids = calloc(IHK_MAX_NUM_NUMA_NODES, sizeof(int));
    CHKANDJUMP(req.numa_ids == NULL, -ENOMEM,
         "failed to allocate torelease\n");

    /* round up not to release too much */
    ave_compensate = ((total_missing / num_numa_nodes_compensate +
           IHKLIB_RESERVE_AMOUNT_ALIGN - 1) /
          IHKLIB_RESERVE_AMOUNT_ALIGN) *
      IHKLIB_RESERVE_AMOUNT_ALIGN;
    dprintk("%s: ave compensate: %ld\n",
      __func__, ave_compensate);

    /* Fill below ave(requested + compensation),
     * compensating nodes upto the average
     */
    for (i = 0; i < IHK_MAX_NUM_NUMA_NODES; i++) {
      if (reserved[i] <= ave_requested) {
        continue;
      }

      if (reserved[i] > ave_requested + ave_compensate) {
        num_numa_nodes_release++;
        total_excess2 += reserved[i] - ave_requested -
          ave_compensate;
        dprintk("%s: above-ave-req+comp: node id: %d, reserved: %ld, ave requested: %ld, ave compensate: %ld, compensate2+=%ld\n",
          __func__, i, reserved[i],
          ave_requested, ave_compensate,
          reserved[i] - ave_requested -
          ave_compensate);
      } else {
        total_missing2 += ave_requested +
          ave_compensate - reserved[i];
        dprintk("%s: below-ave-req+comp: node id: %d, reserved: %ld, ave requested: %ld, ave compensate: %ld, missing2+=%ld\n",
          __func__, i, reserved[i],
          ave_requested, ave_compensate,
          ave_requested + ave_compensate -
          reserved[i]);
      }
    }

    dprintk("%s: total excess2: %ld, total missing2: %ld\n",
      __func__, total_excess2, total_missing2);

    /* round up not to release too much */
    ave_compensate2 =
      ((total_missing2 / num_numa_nodes_release +
        IHKLIB_RESERVE_AMOUNT_ALIGN - 1) /
       IHKLIB_RESERVE_AMOUNT_ALIGN) *
      IHKLIB_RESERVE_AMOUNT_ALIGN;
    dprintk("%s: ave compensate2: %ld\n",
      __func__, ave_compensate2);

    /* above-average-of-requested-plus-compensation nodes
     * can release the excess amount
     */
    for (i = 0; i < IHK_MAX_NUM_NUMA_NODES; i++) {
      req.numa_ids[i] = i;

      if (reserved[i] > ave_requested +
          ave_compensate + ave_compensate2) {
        req.sizes[i] = reserved[i] - ave_requested -
          ave_compensate - ave_compensate2;
        CHKANDJUMP(reserved[i] < ave_requested +
             ave_compensate + ave_compensate2,
             -EINVAL, "negative release size\n");
      } else {
        req.sizes[i] = 0;
      }

      if (req.sizes[i] != 0) {
        dprintk("%s: node id: %d, to-release: %ld\n",
          __func__, i, req.sizes[i]);
      }

      if (reserved[i] > 0 &&
          reserved[i] - req.sizes[i] < min) {
        min = reserved[i] - req.sizes[i];
      }
      if (reserved[i] > 0 &&
          reserved[i] - req.sizes[i] > max) {
        max = reserved[i] - req.sizes[i];
      }
    }

    variance_limit = ave_requested *
      reserve_mem_conf.variance_limit / 100;
    dprintk("%s: min: %ld, max: %ld, variance_limit: %ld\n",
      __func__, min, max, variance_limit);
    if (max - ave_requested > variance_limit ||
        ave_requested - min > variance_limit) {
#ifdef DEBUG
      unsigned long max_ave = max - ave_requested;
      unsigned long ave_min = ave_requested - min;
#endif

      dprintf("%s: error: variance > limit, "
        "ave: %ld (%ld MiB), "
        "max - ave: %ld (%ld MiB), "
        "ave - min: %ld (%ld MiB), "
        "limit: %ld (%ld MiB)\n",
        __func__,
        ave_requested, ave_requested >> 20,
        max_ave, max_ave >> 20,
        ave_min, ave_min >> 20,
        variance_limit, variance_limit >> 20);

      release = 1;
      ret = -ENOMEM;
      goto out;
    }

    req.num_chunks = IHK_MAX_NUM_NUMA_NODES;

    fd = ihklib_device_open(index);
    if (fd < 0) {
      ret = fd;
      dprintf("%s: ihklib_device_open returned %d\n",
        __func__, fd);
    }

    ret = ioctl(fd, IHK_DEVICE_RELEASE_MEM_PARTIALLY, &req);
    if (ret != 0) {
      ret = -errno;
      dprintf("%s: IHK_DEVICE_RESERVE_MEM returned %d\n",
        __func__, -ret);
      goto out;
    }

    close(fd);
  }

  ret = 0;
out:
  if (release) {
    struct ihk_mem_chunk mem_chunks[1] = {
      { .size = -1UL, .numa_node_number = 0 }
    };

    ihk_release_mem(index, mem_chunks, 1);
  }

  if (fd >= 0) {
    close(fd);
  }
  free(req.sizes);
  free(req.numa_ids);
  if (reserve_mem_conf.total) {
    free(mem_chunks_reserved);
    free(reserved);
  }
  return ret;
}

int ihk_reserve_mem(int index, struct ihk_mem_chunk *mem_chunks,
        int num_mem_chunks)
{
  if (get_test_mode() != TEST_IHK_RESERVE_MEM)
    return ihk_reserve_mem_orig(index, mem_chunks, num_mem_chunks);

  int ret, i;

  dprintk("%s: reserve_mem_conf.total=%d\n", __func__, reserve_mem_conf.total);

  unsigned long ivec = 0;
  unsigned long total_branch = 23;

  branch_info_t b_infos[] = {
    { -ENOENT, "branch 1" },
    { -EINVAL, "branch 2" },
    { -EINVAL, "branch 3" },
    { -EINVAL, "branch 4" },
    { -ENOENT, "branch 5" },
    { -ENOMEM, "branch 6" },
    { 0,       "branch 7" },
    { 0,       "branch 8" },
    { 0,       "branch 9" },
    { 0,       "branch 10" },
    { -ENOMEM, "branch 11" },
    { 0,       "branch 12" },
    { 0,       "branch 13" },
    { 0,       "branch 14" },
    { 0,       "branch 15" },
    { 0,       "branch 16" },
    { 0,       "branch 17" },
    { 0,       "branch 18" },
    { -ENOMEM, "branch 19" },
    { -ENOENT, "branch 20" },
    { -EFAULT, "branch 21" },
    { 0,       "reserve_mem_conf.total = 1" },
    { 0,       "reserve_mem_conf.total = 0" }
  };

  int reserve_mem_conf_total_before = reserve_mem_conf.total;
  struct mems mems_expected = { 0 };
  struct mems mems_margin = { 0 };
  ret = mems_init(&mems_margin, num_mem_chunks);
  if (ret) return ret;
  mems_fill(&mems_margin, 4UL << 20);

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    struct ihk_mem_req req = { 0 };
    size_t total_requested = 0;
    int num_mem_chunks_reserved = 0;
    struct ihk_mem_chunk *mem_chunks_reserved = NULL;
    int n_mem_chunks_out = 0;
    size_t *reserved = NULL;
    int num_numa_nodes = 0;
    int num_numa_nodes_compensate = 0;
    int num_numa_nodes_release = 0;
    size_t ave_requested = 0;
    size_t total_missing = 0, total_excess = 0;
    size_t ave_compensate;
    size_t total_missing2 = 0, total_excess2 = 0;
    size_t ave_compensate2 = 0;
    unsigned long min = (unsigned long)-1;
    unsigned long max = 0;
    unsigned long variance_limit = 0;
    int fd = -1;
    int release = 0;
    int should_quit = 0;
    int skip_cleanup = 0;
    int fail = 0;

    ret = ihklib_device_readable(index);
    if (ivec == 0 || ret) {
      ret = -ENOENT;
      if (ivec != 0) should_quit = 1;
      goto out;
    }

    if (ivec == 1 || (num_mem_chunks < 0 || num_mem_chunks > IHK_MAX_NUM_MEM_CHUNKS)) {
      if (ivec != 1) {
        dprintf("%s: error: invalid # of chunks (%d)\n",
          __func__, num_mem_chunks);
        should_quit = 1;
      }
      ret = -EINVAL;
      goto out;
    }

    if (ivec == 2 || (num_mem_chunks != 0 && mem_chunks == NULL)) {
      ret = -EINVAL;
      if (ivec != 2) should_quit = 1;
      goto out;
    }

    if (ivec == 3 || num_mem_chunks == 0) {
      ret = -EINVAL;
      if (ivec != 3) should_quit = 1;
      goto out;
    }

    req.sizes = calloc(num_mem_chunks, sizeof(size_t));
    if (!req.sizes) {
      dprintf("%s: error: allocating req.sizes\n", __func__);
      should_quit = 1;
      ret = -ENOMEM;
      goto out;
    }

    req.numa_ids = calloc(num_mem_chunks, sizeof(int));
    if (!req.numa_ids) {
      dprintf("%s: error: allocating req.numa_ids\n", __func__);
      should_quit = 1;
      ret = -ENOMEM;
      goto out;
    }

    /* fake reserve_mem_conf.total */
    if (ivec >= 6 && ivec < total_branch-1) reserve_mem_conf.total = 1;
    else reserve_mem_conf.total = 0;

    for (i = 0; i < num_mem_chunks; i++) {
      if (reserve_mem_conf.total) {
        req.sizes[i] = (size_t)IHK_SMP_MEM_ALL;
        total_requested += (size_t)mem_chunks[i].size;
      } else {  // for the last branch (ivec = total_branch-1)
        req.sizes[i] = (size_t)mem_chunks[i].size;
      }
      req.numa_ids[i] = mem_chunks[i].numa_node_number;
    }

    req.num_chunks = num_mem_chunks;
    req.min_chunk_size = reserve_mem_conf.min_chunk_size;
    req.max_size_ratio_all = reserve_mem_conf.max_size_ratio_all;
    req.timeout = reserve_mem_conf.timeout;

    fd = ihklib_device_open(index);
    if (ivec == 4 || fd < 0) {
      ret = -ENOENT;
      if (ivec != 4) {
        printf("%s: ihklib_device_open returned %d\n", __func__, fd);
        should_quit = 1;
      }
      goto out;
    }

    if (ivec != 5)
      ret = ioctl(fd, IHK_DEVICE_RESERVE_MEM, &req);

    if (ivec == 5 || ret != 0) {
      ret = -ENOMEM;
      if (ivec != 5) {
        ret = -errno;
        dprintf("%s: IHK_DEVICE_RESERVE_MEM returned %d\n",
          __func__, -ret);
        should_quit = 1;
      }
      goto out;
    }

    close(fd); fd = -1;

    /* 6 <= ivec < total_branch-1  */
    if (reserve_mem_conf.total) {
      should_quit = 1;  // for jumping inside CHKANDJUMP
      release = 1;  // must release reserved mem

      dprintk("%s: total requested: %ld\n", __func__, total_requested);

      num_mem_chunks_reserved = ihk_get_num_reserved_mem_chunks(index);
      mem_chunks_reserved = calloc(num_mem_chunks_reserved,
                                   sizeof(struct ihk_mem_chunk));
      CHKANDJUMP(mem_chunks_reserved == NULL, -ENOMEM,
                 "failed to allocate mem_chunks_reserved\n");

      ret = ihk_query_mem(index, mem_chunks_reserved,
                          num_mem_chunks_reserved);
      CHKANDJUMP(ret, -EINVAL, "ihk_query_mem failed\n");

      reserved = calloc(IHK_MAX_NUM_NUMA_NODES, sizeof(size_t));
      CHKANDJUMP(reserved == NULL, -ENOMEM, "failed to allocate reserved\n");

      for (i = 0; i < num_mem_chunks_reserved; i++) {
        reserved[mem_chunks_reserved[i].numa_node_number] +=
          mem_chunks_reserved[i].size;
      }

      for (i = 0; i < IHK_MAX_NUM_NUMA_NODES; i++) {
        if (ivec == 6 || reserved[i] == 0) {
          continue;
        }
        num_numa_nodes++;
      }

      if (ivec == 6) {
        should_quit = 0;
        OKNG(num_numa_nodes == 0,
             "The number of NUMA nodes (which have reserved memory)"
             " should equal 0\n");
        goto out;
      }

  /* align reserve/release amount */
  #define IHKLIB_RESERVE_AMOUNT_ALIGN (1UL << 20)

      /* round up not to release too much */
      ave_requested = ((total_requested / num_numa_nodes +
        IHKLIB_RESERVE_AMOUNT_ALIGN - 1) /
        IHKLIB_RESERVE_AMOUNT_ALIGN) *
        IHKLIB_RESERVE_AMOUNT_ALIGN;
      dprintk("%s: ave requested: %ld\n",
        __func__, ave_requested);

      /* Fill below-average-of-requested nodes upto the average */
      for (i = 0; i < IHK_MAX_NUM_NUMA_NODES; i++) {
        if (ivec == 7 || reserved[i] == 0) {
          continue;
        }

        dprintk("%s: node id: %d, reserved: %ld\n",
          __func__, i, reserved[i]);
        if (ivec == 8 || (ivec != 9 && reserved[i] > ave_requested)) {
          num_numa_nodes_compensate++;
          if (ivec == 8) total_excess += (reserved[i] > ave_requested)? 0 : (1+ave_requested-reserved[i]);
          total_excess += reserved[i] - ave_requested;
        } else {  // ivec = 9
          if (ivec == 9) total_missing += (reserved[i] <= ave_requested)? 0 : (1+reserved[i]-ave_requested);
          total_missing += ave_requested - reserved[i];
        }
      }
      /* test code for ivec = 7-9 */
      if (ivec == 7) {
        should_quit = 0;
        OKNG(num_numa_nodes_compensate == 0,
             "The number of NUMA nodes which have excess of reserved memory"
             " should equal 0\n");
        OKNG(total_excess == 0, "Total excess should equal 0\n");
        OKNG(total_missing == 0, "Total missing should equal 0\n");
        goto out;
      }

      if (ivec == 8) {
        should_quit = 0;
        OKNG(num_numa_nodes_compensate > 0,
             "The number of NUMA nodes which have excess of reserved memory"
             " should be greater than 0\n");
        OKNG(total_excess > 0, "Total excess should be greater than 0\n");
        OKNG(total_missing == 0, "Total missing should equal 0\n");
      }

      if (ivec == 9) {
        should_quit = 0;
        OKNG(num_numa_nodes_compensate == 0,
             "The number of NUMA nodes which have excess of reserved memory"
             " should equal 0\n");
        OKNG(total_excess == 0, "Total excess should equal 0\n");
        OKNG(total_missing > 0, "Total missing should be greater than 0\n");
        goto out;
      }

      if (ivec == 10 || total_missing > total_excess) {
        if (ivec != 10) {
          dprintf("%s: error: "
                  "sum of below-ave (%ld, %ld MiB) > "
                  "sum of above-ave (%ld, %ld MiB)\n",
                  __func__,
                  total_missing, total_missing >> 20,
                  total_excess, total_excess >> 20);
        } else {
          should_quit = 0;
        }
        ret = -ENOMEM;
        goto out;
      }

      dprintk("%s: total missing: %ld\n", __func__, total_missing);

      req.sizes = calloc(IHK_MAX_NUM_NUMA_NODES, sizeof(size_t));
      CHKANDJUMP(req.sizes == NULL, -ENOMEM, "failed to allocate torelease\n");

      req.numa_ids = calloc(IHK_MAX_NUM_NUMA_NODES, sizeof(int));
      CHKANDJUMP(req.numa_ids == NULL, -ENOMEM, "failed to allocate torelease\n");

      /* round up not to release too much */
      ave_compensate = ((total_missing / num_numa_nodes_compensate +
        IHKLIB_RESERVE_AMOUNT_ALIGN - 1) /
        IHKLIB_RESERVE_AMOUNT_ALIGN) *
        IHKLIB_RESERVE_AMOUNT_ALIGN;
      dprintk("%s: ave compensate: %ld\n", __func__, ave_compensate);

      /* Fill below ave(requested + compensation),
       * compensating nodes upto the average
       */
      for (i = 0; i < IHK_MAX_NUM_NUMA_NODES; i++) {
        if (ivec == 11 || reserved[i] <= ave_requested) {
          continue;
        }

        if (ivec == 12 || (ivec != 13 && reserved[i] > ave_requested + ave_compensate)) {
          num_numa_nodes_release++;
          total_excess2 += reserved[i] - ave_requested -
            ave_compensate;
          if (ivec == 12) total_excess2 += (reserved[i] > ave_requested+ave_compensate)? 0 : (1+ave_requested+ave_compensate-reserved[i]);
          dprintk("%s: above-ave-req+comp: node id: %d, reserved: %ld, ave requested: %ld, ave compensate: %ld, compensate2+=%ld\n",
                  __func__, i, reserved[i],
                  ave_requested, ave_compensate,
                  reserved[i] - ave_requested -
                  ave_compensate);
        } else {  // ivec = 13
          total_missing2 += ave_requested +
            ave_compensate - reserved[i];
          if (ivec == 13) total_missing2 += (reserved[i] <= ave_requested+ave_compensate)? 0 : (1+reserved[i]-ave_requested-ave_compensate);
          dprintk("%s: below-ave-req+comp: node id: %d, reserved: %ld, ave requested: %ld, ave compensate: %ld, missing2+=%ld\n",
            __func__, i, reserved[i],
            ave_requested, ave_compensate,
            ave_requested + ave_compensate -
            reserved[i]);
        }
      }

      /* test code for ivec = 11-13 */
      if (ivec == 11) {
        should_quit = 0;
        OKNG(num_numa_nodes_release == 0,
             "The number of NUMA nodes to be released memory partially"
             " should equal 0\n");
        OKNG(total_excess2 == 0, "Total excess should equal 0\n");
        OKNG(total_missing2 == 0, "Total missing should equal 0\n");
        goto out;
      }
      if (ivec == 12) {
        should_quit = 0;
        OKNG(num_numa_nodes_release > 0,
             "The number of NUMA nodes to be released memory partially"
             " should be greater than 0\n");
        OKNG(total_excess2 > 0, "Total excess should be greater than 0\n");
        OKNG(total_missing2 == 0, "Total missing should equal 0\n");
      }
      if (ivec == 13) {
        should_quit = 0;
        OKNG(num_numa_nodes_release == 0,
             "The number of NUMA nodes to be released memory partially should equal 0\n");
        OKNG(total_excess2 == 0, "Total excess should equal 0\n");
        OKNG(total_missing2 > 0, "Total missing should be greater than 0\n");
        goto out;
      }

      dprintk("%s: total excess2: %ld, total missing2: %ld\n",
              __func__, total_excess2, total_missing2);

      /* round up not to release too much */
      ave_compensate2 =
        ((total_missing2 / num_numa_nodes_release +
        IHKLIB_RESERVE_AMOUNT_ALIGN - 1) /
        IHKLIB_RESERVE_AMOUNT_ALIGN) *
        IHKLIB_RESERVE_AMOUNT_ALIGN;
      dprintk("%s: ave compensate2: %ld\n", __func__, ave_compensate2);

      /* above-average-of-requested-plus-compensation nodes
       * can release the excess amount
       */
      for (i = 0; i < IHK_MAX_NUM_NUMA_NODES; i++) {
        req.numa_ids[i] = i;

        if (ivec == 14 || (ivec != 15 && reserved[i] > ave_requested +
            ave_compensate + ave_compensate2)) {
          req.sizes[i] = reserved[i] - ave_requested -
            ave_compensate - ave_compensate2;
          if (ivec == 14) req.sizes[i] = 1;
          CHKANDJUMP(req.sizes[i] <= 0, -EINVAL, "invalid release size\n");
        } else {  // ivec = 15
          req.sizes[i] = 0;
        }

        if (req.sizes[i] != 0) {
          dprintk("%s: node id: %d, to-release: %ld\n",
                  __func__, i, req.sizes[i]);
        }

        if (ivec == 16 || (reserved[i] > 0 &&
            reserved[i] - req.sizes[i] < min)) {
          min = reserved[i] - req.sizes[i];
          if (ivec == 16) min = 1;
        }
        if (ivec == 17 || (reserved[i] > 0 &&
            reserved[i] - req.sizes[i] > max)) {
          max = reserved[i] - req.sizes[i];
          if (ivec == 17) max = 1;
        }
      }

      /* test code for ivec = 14-17 */
      if (ivec == 14) {
        should_quit = 0;
        for (i = 0; i < IHK_MAX_NUM_NUMA_NODES; i++)
          if (req.sizes[i] == 0) {
            fail = 1; break;
          }
        OKNG(!fail, "Release size should be positive\n");
        goto out;
      }
      if (ivec == 15) {
        should_quit = 0;
        for (i = 0; i < IHK_MAX_NUM_NUMA_NODES; i++)
          if (req.sizes[i] != 0) {
            fail = 1; break;
          }
        OKNG(!fail, "Normal reserved memory should not be released\n");
        goto out;
      }
      if (ivec == 16) {
        should_quit = 0;
        OKNG(min == 1, "Checking min value\n");
        goto out;
      }
      if (ivec == 17) {
        should_quit = 0;
        OKNG(max == 1, "Checking max value\n");
        goto out;
      }

      variance_limit = ave_requested * reserve_mem_conf.variance_limit / 100;
      dprintk("%s: min: %ld, max: %ld, variance_limit: %ld\n",
              __func__, min, max, variance_limit);
      if (ivec == 18 || (max - ave_requested > variance_limit ||
          ave_requested - min > variance_limit)) {
        if (ivec == 18) should_quit = 0;
  #ifdef DEBUG
        unsigned long max_ave = max - ave_requested;
        unsigned long ave_min = ave_requested - min;
  #endif

        if (ivec != 18) {
          dprintf("%s: error: variance > limit, "
                  "ave: %ld (%ld MiB), "
                  "max - ave: %ld (%ld MiB), "
                  "ave - min: %ld (%ld MiB), "
                  "limit: %ld (%ld MiB)\n",
                  __func__,
                  ave_requested, ave_requested >> 20,
                  max_ave, max_ave >> 20,
                  ave_min, ave_min >> 20,
                  variance_limit, variance_limit >> 20);
        }

        release = 1;
        ret = -ENOMEM;
        goto out;
      }

      req.num_chunks = IHK_MAX_NUM_NUMA_NODES;

      if (ivec != 19)
        fd = ihklib_device_open(index);
      if (ivec == 19 || fd < 0) {
        //ret = fd;
        ret = -ENOENT;
        if (ivec != 19)
          dprintf("%s: ihklib_device_open returned %d\n", __func__, fd);
        if (ivec == 19) should_quit = 0;
        goto out;
      }

      if (ivec != 20)
        ret = ioctl(fd, IHK_DEVICE_RELEASE_MEM_PARTIALLY, &req);
      if (ivec == 20 || ret != 0) {
        ret = -EFAULT;
        if (ivec == 20) should_quit = 0;
        if (ivec != 20) {
          ret = -errno;
          dprintf("%s: IHK_DEVICE_RESERVE_MEM returned %d\n", __func__, -ret);
        }
        goto out;
      }
      close(fd); fd = -1;
      should_quit = 0; release = 0;
      goto out;
    }

    release = 0;
   err:
    if (skip_cleanup) return -1;
    should_quit = 1;

   out:
    reserve_mem_conf.total = reserve_mem_conf_total_before;
    skip_cleanup = 1;

    if (release) {
      struct ihk_mem_chunk _mem_chunks[1] = {
        { .size = -1UL, .numa_node_number = 0 }
      };
      fail = ihk_release_mem(index, _mem_chunks, 1);
      if (fail) OKNG(0, "Can not release reserved mem\n");
    }

    if (fd >= 0) {
      close(fd); fd = -1;
    }
    free(req.sizes);
    free(req.numa_ids);

    if (mem_chunks_reserved != NULL) free(mem_chunks_reserved);
    if (reserved != NULL) free(reserved);

    BRANCH_RET_CHK(ret, b_infos[ivec].expected);

    /* check reserved mem at ending of each branch */
    n_mem_chunks_out = ihk_get_num_reserved_mem_chunks(index);
    mems_expected.mem_chunks = mem_chunks;
    mems_expected.num_mem_chunks = num_mem_chunks;
    if (ivec == total_branch-1) {  // reserve_mem_conf.total = 0
      OKNG(n_mem_chunks_out == num_mem_chunks,
           "Check the number of reserved mem chunks. expected: %d, actual: %d\n",
           num_mem_chunks, n_mem_chunks_out);
      fail = mems_check_reserved(&mems_expected, &mems_margin);
      OKNG(!fail, "Memory reserved as expected\n");
    } else if (ivec == 8 || ivec == 12 || ivec == total_branch-2) {
      // reserve_mem_conf.total = 1
      unsigned long sum_expected = 0;
      for (i = 0; i < mems_expected.num_mem_chunks; i++) {
        sum_expected += mems_expected.mem_chunks[i].size;
      }
      fail = mems_check_total(sum_expected);
      OKNG(!fail, "Total amount reserved %lu\n", sum_expected);
      fail = mems_check_var(reserve_mem_conf.variance_limit / (double)100);
      OKNG(!fail, "NUMA-node variation of reserved size\n");
      fail = mems_release();
      if (fail) OKNG(0, "Can not release reserved mem\n");
    } else {  // should revert state
      OKNG(n_mem_chunks_out == 0,
           "Non mem chunks should be reserved. expected: 0, actual: %d\n",
           n_mem_chunks_out);
    }

    if (should_quit) return ret;
  }

  return ret;
}

int ihk_get_num_reserved_mem_chunks_orig(int index)
{
  int ret;
  int fd = -1;
  struct ihk_mem_req req = { 0 };

  dprintk("%s: enter\n", __func__);
  if ((fd = ihklib_device_open(index)) < 0) {
    eprintf("%s: error: ihklib_device_open\n", __func__);
    ret = fd;
    goto out;
  }

  req.num_chunks = 0;   /* means only get num_reserved_mem_chunks */

  ret = ioctl(fd, IHK_DEVICE_QUERY_MEM, &req);
  if (ret) {
    ret = -errno;
    dprintf("%s: IHK_DEVICE_QUERY_MEM returned %d\n", __func__, -ret);
    goto out;
  }

  ret = req.num_chunks;

 out:
  if (fd != -1) {
    close(fd);
  }
  return ret;
}

int ihk_get_num_reserved_mem_chunks(int index)
{
  if (get_test_mode() != TEST_IHK_GET_NUM_RESERVED_MEM_CHUNKS)
    return ihk_get_num_reserved_mem_chunks_orig(index);

  int ret;
  int fd = -1;

  unsigned long ivec = 0;
  unsigned long total_branch = 3;
  int should_quit = 0;

  branch_info_t b_infos[] = {
    { -ENOENT, "cannot open device" },
    { -EINVAL, "invalid parameter" },
    { 0,       "main case" }
  };

  dprintk("%s: enter\n", __func__);

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    struct ihk_mem_req req = { 0 };

    fd = ihklib_device_open(index);
    if (ivec == 0 || fd < 0) {
      if (ivec != 0) {
        eprintf("%s: error: ihklib_device_open\n", __func__);
        should_quit = 1;
      }
      //ret = fd;
      ret = -ENOENT;
      goto out;
    }

    req.num_chunks = 0;   /* means only get num_reserved_mem_chunks */

    ret = ioctl(fd, IHK_DEVICE_QUERY_MEM, &req);
    if (ivec == 1 || ret) {
      if (ivec != 1) {
        ret = -errno;
        dprintf("%s: IHK_DEVICE_QUERY_MEM returned %d\n", __func__, -ret);
        should_quit = 1;
      }
      ret = -EINVAL;
      goto out;
    }

    ret = 0;

   out:
    if (fd != -1) {
      close(fd);
    }

    BRANCH_RET_CHK(ret, b_infos[ivec].expected);

    if (should_quit) return ret;

    ret = req.num_chunks;
  }

  return ret;
 err:
  return -EINVAL;
}

int ihk_query_mem_orig(int index, struct ihk_mem_chunk* mem_chunks, int _num_mem_chunks)
{
  int ret;
  int fd = -1;
  int i;
  int num_mem_chunks;
  struct ihk_mem_req req = { 0 };

  dprintk("%s: enter\n", __func__);
  ret = ihklib_device_readable(index);
  if (ret) {
    goto out;
  }

  if (_num_mem_chunks < 0 || _num_mem_chunks > IHK_MAX_NUM_MEM_CHUNKS) {
    dprintf("%s: error: invalid # of chunks (%d)\n",
            __func__, _num_mem_chunks);
    ret = -EINVAL;
    goto out;
  }

  if (_num_mem_chunks != 0 && mem_chunks == NULL) {
    ret = -EFAULT;
    goto out;
  }

  ret = ihk_get_num_reserved_mem_chunks(index);
  if (ret < 0) {
    dprintf("%s: error: ihk_get_num_reserved_mem_chunks"
            " returned %d\n", __func__, ret);
    goto out;
  }
  num_mem_chunks = ret;

  if (_num_mem_chunks != num_mem_chunks) {
    dprintf("%s: error: actual # of chunks (%d) != requested (%d)\n",
            __func__, num_mem_chunks, _num_mem_chunks);
    ret = -EINVAL;
    goto out;
  }

  req.sizes = calloc(num_mem_chunks, sizeof(size_t));
  if (!req.sizes) {
    dprintf("%s: error: allocating request sizes\n", __func__);
    ret = -ENOMEM;
    goto out;
  }

  req.numa_ids = calloc(num_mem_chunks, sizeof(int));
  if (!req.numa_ids) {
    dprintf("%s: error: allocating request numa_ids\n", __func__);
    ret = -ENOMEM;
    goto out;
  }

  req.num_chunks = num_mem_chunks;

  if ((fd = ihklib_device_open(index)) < 0) {
    dprintf("%s: error: ihklib_device_open returned %d\n", __func__, fd);
    ret = fd;
    goto out;
  }

  ret = ioctl(fd, IHK_DEVICE_QUERY_MEM, &req);
  if (ret) {
    ret = -errno;
    dprintf("%s: error: IHK_DEVICE_QUERY_MEM returned %d\n", __func__, -ret);
    goto out;
  }

  for (i = 0; i < num_mem_chunks; i++) {
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

int ihk_query_mem(int index, struct ihk_mem_chunk* mem_chunks, int _num_mem_chunks)
{
  if (get_test_mode() != TEST_IHK_QUERY_MEM)
    return ihk_query_mem_orig(index, mem_chunks, _num_mem_chunks);

  int ret;
  unsigned long ivec = 0;
  unsigned long total_branch = 8;
  int should_quit = 0;

  branch_info_t b_infos[] = {
    { -ENOENT, "cannot read device" },
    { -EINVAL, "invalid number of chunks" },
    { -EINVAL, "chunks array is null" },
    { -EINVAL, "cannot get reserved mem chunks" },
    { -EINVAL, "number of chunks mismatch" },
    { -ENOENT, "cannot open device" },
    { -EINVAL, "cannot query mem chunks" },
    { 0,       "main case" }
  };

  dprintk("%s: enter\n", __func__);

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    int fd = -1;
    int i;
    int num_mem_chunks;
    struct ihk_mem_req req = { 0 };

    ret = ihklib_device_readable(index);
    if (ivec == 0 || ret) {
      ret = -ENOENT;
      if (ivec != 0) should_quit = 1;
      goto out;
    }

    if (ivec == 1
        || (_num_mem_chunks < 0 || _num_mem_chunks > IHK_MAX_NUM_MEM_CHUNKS)) {
      if (ivec != 1) {
        dprintf("%s: error: invalid # of chunks (%d)\n",
                __func__, _num_mem_chunks);
        should_quit = 1;
      }
      ret = -EINVAL;
      goto out;
    }

    if (ivec == 2 || (_num_mem_chunks != 0 && mem_chunks == NULL)) {
      ret = -EINVAL;
      if (ivec != 2) should_quit = 1;
      goto out;
    }

    ret = ihk_get_num_reserved_mem_chunks(index);
    if (ivec == 3 || ret < 0) {
      if (ivec != 3) {
        dprintf("%s: error: ihk_get_num_reserved_mem_chunks"
                " returned %d\n", __func__, ret);
        should_quit = 1;
      }
      ret = -EINVAL;
      goto out;
    }
    num_mem_chunks = ret;

    if (ivec == 4 || _num_mem_chunks != num_mem_chunks) {
      if (ivec != 4) {
        dprintf("%s: error: actual # of chunks (%d) != requested (%d)\n",
                __func__, num_mem_chunks, _num_mem_chunks);
        should_quit = 1;
      }
      ret = -EINVAL;
      goto out;
    }

    req.sizes = calloc(num_mem_chunks, sizeof(size_t));
    if (!req.sizes) {
      dprintf("%s: error: allocating request sizes\n", __func__);
      should_quit = 1;
      ret = -ENOMEM;
      goto out;
    }

    req.numa_ids = calloc(num_mem_chunks, sizeof(int));
    if (!req.numa_ids) {
      dprintf("%s: error: allocating request numa_ids\n", __func__);
      should_quit = 1;
      ret = -ENOMEM;
      goto out;
    }

    req.num_chunks = num_mem_chunks;

    fd = ihklib_device_open(index);
    if (ivec == 5 || fd < 0) {
      if (ivec != 5) {
        dprintf("%s: error: ihklib_device_open returned %d\n", __func__, fd);
        should_quit = 1;
      }
      //ret = fd;
      ret = -ENOENT;
      goto out;
    }

    ret = ioctl(fd, IHK_DEVICE_QUERY_MEM, &req);
    if (ivec == 6 || ret) {
      if (ivec != 6) {
        ret = -errno;
        dprintf("%s: error: IHK_DEVICE_QUERY_MEM returned %d\n", __func__, -ret);
        should_quit = 1;
      }
      ret = -EINVAL;
      goto out;
    }

    for (i = 0; i < num_mem_chunks; i++) {
      mem_chunks[i].size = req.sizes[i];
      mem_chunks[i].numa_node_number = req.numa_ids[i];
    }

   out:
    if (fd != -1) {
      close(fd);
    }
    if (req.sizes) free(req.sizes);
    if (req.numa_ids) free(req.numa_ids);

    BRANCH_RET_CHK(ret, b_infos[ivec].expected);
  }

  return ret;
 err:
  return -EINVAL;
}

int ihk_release_mem_orig(int index, struct ihk_mem_chunk* mem_chunks, int num_mem_chunks)
{
  int ret, i;
  struct ihk_mem_req req = { 0 };
  int fd = -1;
  struct ihk_mem_chunk *query_mem_chunks = NULL;

  dprintk("%s: enter\n", __func__);

  ret = ihklib_device_readable(index);
  if (ret) {
    goto out;
  }

  if (num_mem_chunks < 0 || num_mem_chunks > IHK_MAX_NUM_MEM_CHUNKS) {
    dprintf("%s: error: invalid # of chunks (%d)\n",
            __func__, num_mem_chunks);
    ret = -EINVAL;
    goto out;
  }

  if (num_mem_chunks != 0 && mem_chunks == NULL) {
    ret = -EFAULT;
    goto out;
  }

  if (num_mem_chunks == 0) {
    ret = 0;
    goto out;
  };

  if (mem_chunks[0].size == IHK_SMP_MEM_ALL) {
    /* Special case for releasing all memory */
    num_mem_chunks = ihk_get_num_reserved_mem_chunks(index);
    query_mem_chunks = calloc(num_mem_chunks, sizeof(struct ihk_mem_chunk));
    if (query_mem_chunks == NULL) {
      dprintf("%s: error: allocating memory chunks\n", __func__);
      ret = -ENOMEM;
      goto out;
    }

    ret = ihk_query_mem(index, query_mem_chunks, num_mem_chunks);
    if (ret) {
      dprintf("%s: error: ihk_query_mem returned %d\n", __func__, ret);
      goto out;
    }

    mem_chunks = query_mem_chunks;
  }

  req.sizes = calloc(num_mem_chunks, sizeof(size_t));
  if (!req.sizes) {
    dprintf("%s: error: allocating request sizes\n", __func__);
    ret = -ENOMEM;
    goto out;
  }

  req.numa_ids = calloc(num_mem_chunks, sizeof(int));
  if (!req.numa_ids) {
    dprintf("%s: error: allocating request numa_ids\n", __func__);
    ret = -ENOMEM;
    goto out;
  }

  for (i = 0; i < num_mem_chunks; i++) {
    req.sizes[i] = (size_t)mem_chunks[i].size;
    req.numa_ids[i] = mem_chunks[i].numa_node_number;
  }
  req.num_chunks = num_mem_chunks;

  if ((fd = ihklib_device_open(index)) < 0) {
    dprintf("%s: error: ihklib_device_open\n", __func__);
    ret = fd;
    goto out;
  }

  ret = ioctl(fd, IHK_DEVICE_RELEASE_MEM, &req);
  if (ret) {
    ret = -errno;
    dprintf("%s: error: IHK_OS_RELEASE_MEM returned %d\n", __func__, -ret);
    goto out;
  }

 out:
  if (fd != -1) {
    close(fd);
  }
  free(query_mem_chunks);
  free(req.sizes);
  free(req.numa_ids);
  return ret;
}

int ihk_release_mem(int index, struct ihk_mem_chunk* mem_chunks, int num_mem_chunks)
{
  if (get_test_mode() != TEST_IHK_RELEASE_MEM)
    return ihk_release_mem_orig(index, mem_chunks, num_mem_chunks);

  int ret, i;

  unsigned long ivec = 0;
  unsigned long total_branch = 10;

  branch_info_t b_infos[] = {
    { -ENOENT, "cannot read device" },
    { -EINVAL, "invalid number of mem chunks" },
    { -EINVAL, "chunks buffer is null" },
    { 0,       "the number of chunks is zero" },
    { -ENOENT, "cannot open device" },
    { -EINVAL, "release query error" },
    { 0,       "release parts of reserved chunks success" },
    { 0,       "no any chunks to release" },    // IHK_SMP_MEM_ALL
    { -EINVAL, "cannot query mem" },            // IHK_SMP_MEM_ALL
    { 0,       "release all chunks success" },  // IHK_SMP_MEM_ALL
  };

  dprintk("%s: enter\n", __func__);

  /* save previous state */
  struct mems mems_before = { 0 }, mems_after = { 0 };
  mems_reserved(&mems_before);
  long sum_before[MAX_NUM_MEM_CHUNKS] = { 0 };
  for (i = 0; i < mems_before.num_mem_chunks; i++) {
    sum_before[mems_before.mem_chunks[i].numa_node_number] +=
      mems_before.mem_chunks[i].size;
  }

  struct ihk_mem_req req = { 0 };
  struct ihk_mem_chunk *query_mem_chunks = NULL;

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    long sum_after[MAX_NUM_MEM_CHUNKS]  = { 0 };
    long sum_left[MAX_NUM_MEM_CHUNKS]   = { 0 };
    int should_quit = 0;
    int fd = -1;
    req.sizes = NULL;
    req.numa_ids = NULL;
    query_mem_chunks = NULL;
    mems_after.num_mem_chunks = 0;

    ret = ihklib_device_readable(index);
    if (ivec == 0 || ret) {
      if (ivec != 0) should_quit = 1;
      ret = -ENOENT;
      goto out;
    }

    if (ivec == 1
        || (num_mem_chunks < 0 || num_mem_chunks > IHK_MAX_NUM_MEM_CHUNKS)) {
      if (ivec != 1) {
        dprintf("%s: error: invalid # of chunks (%d)\n",
                __func__, num_mem_chunks);
        should_quit = 1;
      }
      ret = -EINVAL;
      goto out;
    }

    if (ivec == 2 || (num_mem_chunks != 0 && mem_chunks == NULL)) {
      if (ivec != 2) should_quit = 1;
      ret = -EINVAL;
      goto out;
    }

    if (ivec == 3 || num_mem_chunks == 0) {
      if (ivec != 3) should_quit = 1;
      ret = 0;
      goto out;
    };

    /* the last 3 branches will attemp to release all chunks */
    if (ivec >= total_branch - 3) mem_chunks[0].size = IHK_SMP_MEM_ALL;

    if (mem_chunks[0].size == IHK_SMP_MEM_ALL) {
      /* Special case for releasing all memory */
      num_mem_chunks = ihk_get_num_reserved_mem_chunks(index);
      if (ivec == total_branch-3 || num_mem_chunks <= 0) {
        if (ivec != total_branch-3) should_quit = 1;
        ret = 0;
        goto out;
      }

      query_mem_chunks = calloc(num_mem_chunks, sizeof(struct ihk_mem_chunk));
      if (query_mem_chunks == NULL) {
        dprintf("%s: error: allocating memory chunks\n", __func__);
        should_quit = 1;
        ret = -ENOMEM;
        goto out;
      }

      ret = ihk_query_mem(index, query_mem_chunks, num_mem_chunks);
      if (ivec == total_branch-2 || ret) {
        if (ivec != total_branch-2) {
          dprintf("%s: error: ihk_query_mem returned %d\n", __func__, ret);
          should_quit = 1;
        }
        ret = -EINVAL;
        goto out;
      }
      // the last branch goes from here
      mem_chunks = query_mem_chunks;
    }

    req.sizes = calloc(num_mem_chunks, sizeof(size_t));
    if (!req.sizes) {
      dprintf("%s: error: allocating request sizes\n", __func__);
      should_quit = 1;
      ret = -ENOMEM;
      goto out;
    }

    req.numa_ids = calloc(num_mem_chunks, sizeof(int));
    if (!req.numa_ids) {
      dprintf("%s: error: allocating request numa_ids\n", __func__);
      should_quit = 1;
      ret = -ENOMEM;
      goto out;
    }

    for (i = 0; i < num_mem_chunks; i++) {
      req.sizes[i] = (size_t)mem_chunks[i].size;
      req.numa_ids[i] = mem_chunks[i].numa_node_number;
    }
    req.num_chunks = num_mem_chunks;

    fd = ihklib_device_open(index);
    if (ivec == 4 || fd < 0) {
      if (ivec != 4) {
        dprintf("%s: error: ihklib_device_open\n", __func__);
        should_quit = 1;
      }
      ret = -ENOENT;
      goto out;
    }

    if (ivec != 5)
      ret = ioctl(fd, IHK_DEVICE_RELEASE_MEM, &req);
    if (ivec == 5 || ret) {
      if (ivec != 5) {
        ret = -errno;
        dprintf("%s: error: IHK_OS_RELEASE_MEM returned %d\n", __func__, -ret);
        should_quit = 1;
      }
      ret = -EINVAL;
      goto out;
    }

   out:
    if (fd != -1) {
      close(fd);
    }
    if (should_quit) goto err;
    should_quit = 1;

    BRANCH_RET_CHK(ret, b_infos[ivec].expected);

    /* check current state */
    mems_reserved(&mems_after);
    for (i = 0; i < mems_after.num_mem_chunks; i++) {
      sum_after[mems_after.mem_chunks[i].numa_node_number] += mems_after.mem_chunks[i].size;
    }

    if (ivec == 6 || ivec == total_branch-1) {  // there're only 2 success branches
      if (ivec == total_branch-1) {  // should release all chunks
        OKNG(mems_after.num_mem_chunks == 0, "all mem chunks are released\n");
      }
      if (ivec == 6) {
        OKNG(mems_before.num_mem_chunks - mems_after.num_mem_chunks == num_mem_chunks,
             "the number of reserved mem chunks should be decreased by %d\n", num_mem_chunks);

        for (i = 0; i < num_mem_chunks; i++) {
         sum_left[mem_chunks[i].numa_node_number] += mem_chunks[i].size;
        }
        int fail = 0;
        for (i = 0; i < MAX_NUM_MEM_CHUNKS; i++) {
          if (sum_left[i] == 0) continue;
          if (sum_left[i] + sum_after[i] != sum_before[i]) {
            fail = 1; break;
          }
        }
        OKNG(!fail, "check chunks size matching\n");

        /* try to reserve released memory again for next branches.
         * this can cost much time. */
        int conf_total_prev = reserve_mem_conf.total;
        reserve_mem_conf.total = 0;
        ret = ihk_reserve_mem(index, mem_chunks, num_mem_chunks);
        reserve_mem_conf.total = conf_total_prev;
        OKNG(ret == 0, "can reserve released memory again for using in next branches\n");
      }
    } else {
      OKNG(mems_before.num_mem_chunks == mems_after.num_mem_chunks,
           "the number of chunks should not be changed\n");
      struct mems mems_margin = { 0 };
      ret = mems_init(&mems_margin, mems_before.num_mem_chunks);
      if (ret) goto err;
      mems_fill(&mems_margin, 4UL << 20);
      int fail = mems_compare(&mems_after, &mems_before, &mems_margin);
      OKNG(!fail, "mem chunks should not be changed\n");
    }
    should_quit = 0;

   err:
    if (query_mem_chunks) free(query_mem_chunks);
    if (req.sizes) free(req.sizes);
    if (req.numa_ids) free(req.numa_ids);
    if (should_quit) return (ret)? ret : -EINVAL;
  }

  return 0;
}

int ihk_create_os_orig(int index)
{
  int ret;
  int fd = -1;

  dprintk("%s: enter\n", __func__);
  if ((fd = ihklib_device_open(index)) < 0) {
    dprintf("%s: error: ihklib_device_open\n", __func__);
    ret = fd;
    goto out;
  }

  ret = ioctl(fd, IHK_DEVICE_CREATE_OS, 0);
  if (ret < 0) {
    ret = -errno;
    dprintf("%s: error: IHK_DEVICE_CREATE_OS returned %d\n", __func__, -ret);
    goto out;
  }
 out:
  if (fd != -1) {
    close(fd);
  }
  return ret;
}

/* Create OS and return OS index */
int ihk_create_os(int index)
{
  if (get_test_mode() != TEST_IHK_CREATE_OS)
    return ihk_create_os_orig(index);

  dprintk("%s: enter\n", __func__);

  unsigned long ivec = 0;
  unsigned long total_branch = 3;

  branch_info_t b_infos[] = {
    { -ENOENT, "cannot open device" },
    { -EINVAL, "cannot create os instance" },
    { 0,       "main case" },
  };

  /* get previous state */
  int num_os_before;
  int *indices_before = NULL;
  num_os_before = ihk_get_num_os_instances(index);
  if (num_os_before < 0) return -ENOENT;
  indices_before = calloc(num_os_before, sizeof(int));
  if (!indices_before) return -ENOMEM;
  int ret = ihk_get_os_instances(index, indices_before, num_os_before);
  if (ret) {
    free(indices_before);
    return ret;
  }

  for (ivec = 0; ivec < total_branch; ivec++) {
    START(b_infos[ivec].name);

    int should_quit = 0;
    int *indices_after = NULL;
    int num_os_after = 0;
    int os_index = -1;
    ret = 0;

    int fd = ihklib_device_open(index);
    if (ivec == 0 || fd < 0) {
      if (ivec != 0) {
        dprintf("%s: error: ihklib_device_open\n", __func__);
        should_quit = 1;
      }
      ret = -ENOENT;
      goto out;
    }

    if (ivec != 1)
      ret = ioctl(fd, IHK_DEVICE_CREATE_OS, 0);
    if (ivec == 1 || ret < 0) {
      if (ivec != 1) {
        ret = -errno;
        dprintf("%s: error: IHK_DEVICE_CREATE_OS returned %d\n", __func__, -ret);
        should_quit = 1;
      }
      ret = -EINVAL;
      goto out;
    }
    os_index = ret;
    ret = 0;

   out:
    if (fd != -1) {
      close(fd);
    }

    if (should_quit) goto err;
    should_quit = 1;

    BRANCH_RET_CHK(ret, b_infos[ivec].expected);
    ret = os_index;

    /* check current state */
    num_os_after = ihk_get_num_os_instances(index);
    if (num_os_after < 0) goto err;
    if (num_os_after > 0) {
      indices_after = calloc(num_os_after, sizeof(int));
      if (!indices_after) goto err;
      ret = ihk_get_os_instances(index, indices_after, num_os_after);
      if (ret) goto err;
    }

    /* sort to compare */
    arr_sort(indices_before, num_os_before);
    arr_sort(indices_after, num_os_after);

    if (ivec == total_branch - 1) {
      OKNG(num_os_after == num_os_before + 1,
           "the number of os instances should be increased by 1\n");
      OKNG(arr_has_unique_elements(indices_after, num_os_after),
           "os instances should have unique indices\n");

      /* checking indices */
      int pos = arr_first_diff_pos(indices_before, indices_after, num_os_before);
      pos = (pos < 0)? num_os_after-1 : pos;
      OKNG(os_index == indices_after[pos],
           "checking created os index. expected: %d, real: %d\n",
           os_index, indices_after[pos]);
      int *indices_tmp = calloc(num_os_after, sizeof(int));
      if (!indices_tmp) goto err;
      arr_copy_and_add(indices_tmp, indices_before, num_os_before, indices_after[pos]);
      int suc = arr_equals(indices_tmp, indices_after, num_os_after);
      free(indices_tmp);
      OKNG(suc, "checking indices\n");
    } else {
      OKNG(num_os_after == num_os_before,
           "the number of os instances should not be changed\n");
      if (num_os_before > 0) {
        int match = arr_equals(indices_before, indices_after, num_os_before);
        OKNG(match, "os indices should not be changed\n");
      }
    }
    should_quit = 0;

   err:
    if (indices_after) free(indices_after);
    if (should_quit) {
      free(indices_before);
      return (ret < 0)? ret : -EINVAL;
    }
  }

  return ret;
}

int ihk_get_num_os_instances_orig(int index)
{
  int ret;
  DIR *dir = NULL;
  struct dirent *direp;
  int num_os_instances = 0;
  int fd = -1;

  dprintk("%s: enter\n", __func__);
  if ((fd = ihklib_device_open(index)) < 0) {
    dprintf("%s: error: ihklib_device_open\n",
      __func__);
    ret = fd;
    goto out;
  }

  dir = opendir(PATH_DEV);
  if (dir == NULL) {
    ret = -errno;
    dprintf("%s: error: opendir returned %d\n",
      __func__, -ret);
    goto out;
  }

  while ((direp = readdir(dir))) {
    if ((strncmp(direp->d_name,"mcos",4) == 0)) {
      num_os_instances++;
    }
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

int ihk_get_num_os_instances(int index)
{
  if (get_test_mode() != TEST_IHK_GET_NUM_OS_INSTANCES)
    return ihk_get_num_os_instances_orig(index);

  int ret = 0;
  int should_quit = 0;

  dprintk("%s: enter\n", __func__);

  unsigned long ivec = 0;
  unsigned long total_branch = 5;

  branch_info_t b_infos[] = {
    { -ENOENT, "cannot open device" },
    { -ENOENT, "cannot open dir" },
    { -ENOENT, "cannot read dir" },
    { 0,       "not found any os instances" },
    { 0,       "main case" },
  };

  for (ivec = 0; ivec < total_branch; ivec++) {
    START(b_infos[ivec].name);

    DIR *dir = NULL;
    struct dirent *direp;
    int num_os_instances = 0;
    int fd = -1;

    fd = ihklib_device_open(index);
    if (ivec == 0 || fd < 0) {
      if (ivec != 0) {
        dprintf("%s: error: ihklib_device_open\n", __func__);
        should_quit = 1;
      }
      //ret = fd;
      ret = -ENOENT;
      goto out;
    }

    dir = opendir(PATH_DEV);
    if (ivec == 1 || dir == NULL) {
      if (ivec != 1) {
        ret = -errno;
        dprintf("%s: error: opendir returned %d\n", __func__, -ret);
        should_quit = 1;
      }
      ret = -ENOENT;
      goto out;
    }

    direp = readdir(dir);
    if (ivec == 2 || !direp) {
      ret = -ENOENT;
      if (ivec != 2) should_quit = 1;
      goto out;
    }

    while (direp) {
      int found = (strncmp(direp->d_name, "mcos", 4) == 0);
      if (ivec == 3 || !found) {
        goto next_entry;
      }

      num_os_instances++;

     next_entry:
      direp = readdir(dir);
    }

    ret = 0;

   out:
    if (fd != -1) {
      close(fd);
    }
    if (dir) {
      closedir(dir);
    }

    if (should_quit) return ret;

    BRANCH_RET_CHK(ret, b_infos[ivec].expected);

    if (ivec == total_branch - 1) {
      OKNG(num_os_instances > 0, "found some os instances\n");
      ret = num_os_instances;
    } else {
      OKNG(num_os_instances == 0, "not found any os instances\n");
    }
  }

  return ret;
 err:
  return (ret < 0)? ret : -EINVAL;
}

int ihk_get_os_instances_orig(int index, int *indices, int _num_os_instances)
{
  int ret;
  DIR *dir = NULL;
  struct dirent *direp;
  int num_os_instances = 0;
  int num_mcos = 0;

  dprintk("%s: enter\n", __func__);
  ret = ihklib_device_readable(index);
  if (ret) {
    dprintf("%s: error: ihklib_device_readable returned %d\n",
            __func__, ret);
    goto out;
  }

  ret = ihk_get_num_os_instances(index);
  if (ret < 0) {
    dprintf("%s: error: ihk_get_num_os_instances returned %d\n",
            __func__, ret);
    goto out;
  }
  num_os_instances = ret;

  if (num_os_instances != _num_os_instances) {
    dprintf("%s: Actual # of OS instances (%d) != "
            "requested (%d)\n",
            __func__, num_os_instances, _num_os_instances);
    ret = -EINVAL;
    goto out;
  }

  dir = opendir(PATH_DEV);
  if (dir == NULL) {
    ret = -errno;
    dprintf("%s: error: opendir returned %d\n",
            __func__, -ret);
    goto out;
  }

  while ((direp = readdir(dir))) {
    if ((strncmp(direp->d_name, "mcos", 4) == 0)) {
      indices[num_mcos] = atoi(direp->d_name + 4);
      num_mcos++;
    }
  }

  ret = 0;
 out:
  if (dir) {
    closedir(dir);
  }
  return ret;
}

int ihk_get_os_instances(int index, int *indices, int _num_os_instances)
{
  if (get_test_mode() != TEST_IHK_GET_OS_INSTANCES)
    return ihk_get_os_instances_orig(index, indices, _num_os_instances);

  int ret = 0;
  int should_quit = 0;

  dprintk("%s: enter\n", __func__);

  unsigned long ivec = 0;
  unsigned long total_branch = 8;

  branch_info_t b_infos[] = {
    { -ENOENT, "cannot read device" },
    { -EINVAL, "cannot get number of os instances" },
    { -EINVAL, "number of os instances mismatch" },
    { -EINVAL, "indices buffer is null" },
    { -ENOENT, "cannot open dir" },
    { -ENOENT, "cannot read dir" },
    { 0,       "not found any os instances" },
    { 0,       "main case" },
  };

  for (ivec = 0; ivec < total_branch; ivec++) {
    START(b_infos[ivec].name);

    DIR *dir = NULL;
    struct dirent *direp;
    int num_os_instances = 0;
    int num_mcos = 0;

    ret = ihklib_device_readable(index);
    if (ivec == 0 || ret) {
      if (ivec != 0) {
        dprintf("%s: error: ihklib_device_readable returned %d\n",
                __func__, ret);
        should_quit = 1;
      }
      ret = -ENOENT;
      goto out;
    }

    ret = ihk_get_num_os_instances(index);
    if (ivec == 1 || ret < 0) {
      if (ivec != 1) {
        dprintf("%s: error: ihk_get_num_os_instances returned %d\n",
                __func__, ret);
        should_quit = 1;
      }
      ret = -EINVAL;
      goto out;
    }
    num_os_instances = ret;

    if (ivec == 2 || num_os_instances != _num_os_instances) {
      if (ivec != 2) {
        dprintf("%s: Actual # of OS instances (%d) != "
                "requested (%d)\n",
                __func__, num_os_instances, _num_os_instances);
        should_quit = 1;
      }
      ret = -EINVAL;
      goto out;
    }

    if (ivec == 3 || !indices) {
      if (ivec != 3) should_quit = 1;
      ret = -EINVAL;
    }

    dir = opendir(PATH_DEV);
    if (ivec == 4 || dir == NULL) {
      if (ivec != 4) {
        ret = -errno;
        dprintf("%s: error: opendir returned %d\n",
                __func__, -ret);
        should_quit = 1;
      }
      ret = -ENOENT;
      goto out;
    }

    direp = readdir(dir);
    if (ivec == 5 || !direp) {
      if (ivec != 5) should_quit = 1;
      ret = -ENOENT;
      goto out;
    }

    while (direp) {
      int found = (strncmp(direp->d_name, "mcos", 4) == 0);
      if (ivec == 6 || !found) {
        goto next_entry;
      }

      indices[num_mcos] = atoi(direp->d_name + 4);
      num_mcos++;

     next_entry:
      direp = readdir(dir);
    }

    ret = 0;
   out:
    if (dir) {
      closedir(dir);
    }

    if (should_quit) return ret;

    BRANCH_RET_CHK(ret, b_infos[ivec].expected);

    if (ivec == total_branch - 1) {
      OKNG(num_mcos > 0, "found some os instances\n");
      OKNG(num_mcos == _num_os_instances, "matching number of os instances\n");
      /* check redundant */
      int suc = arr_has_unique_elements(indices, num_mcos);
      OKNG(suc, "os instances should have unique ids\n");
    } else {
      OKNG(num_mcos == 0, "not found any os instances\n");
    }
  }

  return ret;
 err:
  return (ret < 0)? ret : -EINVAL;
}

int ihk_destroy_os_orig(int dev_index, int os_index)
{
	int ret;
	int fd = -1;

	dprintk("%s: enter\n", __func__);

	fd = ihklib_device_open(dev_index);
	if (fd < 0) {
		dprintf("%s: error: ihklib_device_open returned %d\n",
			__func__, fd);
		ret = fd;
		goto out;
	}

	ret = ioctl(fd, IHK_DEVICE_DESTROY_OS, os_index);
	if (ret) {
		ret = -errno;
		dprintf("%s: error: IHK_DEVICE_DESTROY_OS returned %d\n",
			__func__, -ret);
		goto out;
	}
 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}

int ihk_destroy_os(int dev_index, int os_index)
{
  if (get_test_mode() != TEST_IHK_DESTROY_OS)
    return ihk_destroy_os_orig(dev_index, os_index);

  unsigned long ivec = 0;
  unsigned long total_branch = 3;

  branch_info_t b_infos[] = {
    { -ENOENT, "cannot open device" },
    { -EINVAL, "cannot destroy instance" },
    { 0,       "main case" },
  };

  dprintk("%s: enter\n", __func__);

  /* save previous state */
  int n_os_prev = ihk_get_num_os_instances(dev_index);
  if (n_os_prev <= 0) return -EINVAL;
  int n_os_after = 0;
  int *indices_prev = calloc(n_os_prev, sizeof(int));
  if (!indices_prev) return -ENOMEM;
  int *indices_after = NULL;
  int ret = ihk_get_os_instances(dev_index, indices_prev, n_os_prev);
  if (ret) {
    free(indices_prev);
    return ret;
  }

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    ret = 0;
    int should_quit = 0;
    int fd = ihklib_device_open(dev_index);
    if (ivec == 0 || fd < 0) {
      ret = -ENOENT;
      if (ivec != 0) {
        dprintf("%s: error: ihklib_device_open returned %d\n",
                __func__, fd);
        should_quit = 1;
      }
      goto out;
    }

    if (ivec != 1)
      ret = ioctl(fd, IHK_DEVICE_DESTROY_OS, os_index);
    if (ivec == 1 || ret) {
      ret = -EINVAL;
      if (ivec != 1) {
        ret = -errno;
        dprintf("%s: error: IHK_DEVICE_DESTROY_OS returned %d\n",
                __func__, -ret);
        should_quit = 1;
      }
      goto out;
    }

   out:
    if (fd != -1) {
      close(fd);
    }
    if (should_quit) goto err;
    should_quit = 1;

    BRANCH_RET_CHK(ret, b_infos[ivec].expected);

    /* check current state */
    n_os_after = ihk_get_num_os_instances(dev_index);
    if (n_os_after < 0) goto err;
    if (n_os_after > 0) {
      indices_after = calloc(n_os_after, sizeof(int));
      if (!indices_after) goto err;
      ret = ihk_get_os_instances(dev_index, indices_after, n_os_after);
      if (ret) goto err;
    }

    if (ivec == total_branch - 1) {
      OKNG(n_os_after == n_os_prev - 1,
           "the number of os instances should be decreased by 1\n");

      /* checking indices */
      arr_sort(indices_prev, n_os_prev);
      if (n_os_after) {
        arr_sort(indices_after, n_os_after);
        int pos = arr_first_diff_pos(indices_prev, indices_after, n_os_after);
        pos = (pos < 0)? n_os_after : pos;
        OKNG(os_index == indices_prev[pos],
             "checking destroyed os index. expected: %d, real: %d\n",
             os_index, indices_prev[pos]);
        int *indices_tmp = calloc(n_os_prev, sizeof(int));
        if (!indices_tmp) goto err;
        arr_copy_and_add(indices_tmp, indices_after, n_os_after, indices_prev[pos]);
        int suc = arr_equals(indices_tmp, indices_prev, n_os_prev);
        free(indices_tmp);
        OKNG(suc, "checking indices\n");
      }
    } else {
      OKNG(n_os_after == n_os_prev,
           "the number of os instances should not be changed\n");
      OKNG(arr_equals(indices_after, indices_prev, n_os_prev),
           "os indices should not be changed\n");
    }
    should_quit = 0;

   err:
    if (indices_after) { free(indices_after); indices_after = NULL; }
    if (should_quit) {
      free(indices_prev);
      return (ret)? ret : -EINVAL;
    }
  }

  return 0;
}

static int ihklib_os_readable(int index)
{
  int ret;
  char fn[PATH_MAX];

  sprintf(fn, "/dev/mcos%d", index);
  ret = access(fn, R_OK);
  if (ret) {
    ret = -errno;
    dprintf("%s: error: access: path: %s, errno: %d\n",
      __func__, fn, -ret);
    goto out;
  }

 out:
  return ret;
}

int ihklib_os_open(int index)
{
  int ret;
  char fn[PATH_MAX];

  ret = ihklib_os_readable(index);
  if (ret) {
    dprintf("%s: error: ihklib_os_readable returned %d\n",
      __func__, ret);
    goto out;
  }

  sprintf(fn, "/dev/mcos%d", index);

  if ((ret = open(fn, O_RDONLY)) == -1) {
    ret = -errno;
    dprintf("%s: error: open %s: %s\n",
      __func__, fn, strerror(-ret));
    goto out;
  }

 out:
  return ret;
}

int ihk_os_assign_cpu_orig(int index, int* cpus, int num_cpus)
{
  int ret;
  struct ihk_ioctl_cpu_desc req = { 0 };
  int fd = -1;

  dprintk("%s: enter\n", __func__);

  ret = ihklib_os_readable(index);
  if (ret) {
    goto out;
  }

  if (num_cpus < 0 || num_cpus > IHK_MAX_NUM_CPUS) {
    dprintf("%s: error: invalid # of cpus (%d)\n",
      __func__, num_cpus);
    ret = -EINVAL;
    goto out;
  }

  if (num_cpus != 0 && cpus == NULL) {
    ret = -EFAULT;
    goto out;
  }

  if (num_cpus == 0) {
    ret = 0;
    goto out;
  }

  req.cpus = cpus;
  req.num_cpus = num_cpus;

  if ((fd = ihklib_os_open(index)) < 0) {
    dprintf("%s: error: ihklib_os_open returned %d\n",
      __func__, fd);
    ret = fd;
    goto out;
  }

  ret = ioctl(fd, IHK_OS_ASSIGN_CPU, &req);
  if (ret) {
    ret = -errno;
    dprintf("%s: error: IHK_OS_ASSIGN_CPU returned %d\n",
      __func__, -ret);
    goto out;
  }

 out:
  if (fd != -1) {
    close(fd);
  }
  return ret;
}

int ihk_os_assign_cpu(int index, int* cpus, int num_cpus)
{
  if (get_test_mode() != TEST_IHK_OS_ASSIGN_CPU)
    return ihk_os_assign_cpu_orig(index, cpus, num_cpus);

  unsigned long ivec = 0;
  unsigned long total_branch = 2;

  branch_info_t b_infos[] = {
    { -ENOENT, "ihklib_os_open fail" },
    { 0,       "main case" },
  };

  dprintk("%s: enter\n", __func__);

  int ret = 0;
  /* save previous state */
  int n_cpus_assigned_prev = ihk_os_get_num_assigned_cpus(index);
  int n_cpus_reserved_prev = ihk_get_num_reserved_cpus(index);
  if (n_cpus_assigned_prev < 0 || n_cpus_reserved_prev <= 0) return -EINVAL;
  int *cpus_assigned_prev = NULL, *cpus_reserved_prev = NULL;
  cpus_reserved_prev = calloc(n_cpus_reserved_prev, sizeof(int));
  if (!cpus_reserved_prev) return -ENOMEM;
  ret = ihk_query_cpu(index, cpus_reserved_prev, n_cpus_reserved_prev);
  if (ret) {
    free(cpus_reserved_prev); return ret;
  }
  if (n_cpus_assigned_prev > 0) {
    cpus_assigned_prev = calloc(n_cpus_assigned_prev, sizeof(int));
    if (!cpus_assigned_prev) {
      free(cpus_reserved_prev); return -ENOMEM;
    }
    ret = ihk_os_query_cpu(index, cpus_assigned_prev, n_cpus_assigned_prev);
    if (ret) {
      free(cpus_reserved_prev);
      free(cpus_assigned_prev);
      return ret;
    }
  }

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    struct ihk_ioctl_cpu_desc req = { 0 };
    int fd = -1;
    int should_quit = 0;
    int n_cpus_assigned_after = 0, n_cpus_reserved_after = 0;
    int *cpus_assigned_after = NULL, *cpus_reserved_after = NULL;

    ret = ihklib_os_readable(index);
    if (ret) {
      goto out;
    }

    if (num_cpus < 0 || num_cpus > IHK_MAX_NUM_CPUS) {
      dprintf("%s: error: invalid # of cpus (%d)\n",
        __func__, num_cpus);
      ret = -EINVAL;
      goto out;
    }

    if (num_cpus != 0 && cpus == NULL) {
      ret = -EFAULT;
      goto out;
    }

    if (num_cpus == 0) {
      ret = 0;
      goto out;
    }

    req.cpus = cpus;
    req.num_cpus = num_cpus;

    fd = ihklib_os_open(index);
    if (ivec == 0 || fd < 0) {
      ret = -ENOENT;
      if (ivec != 0) {
        dprintf("%s: error: ihklib_os_open returned %d\n", __func__, fd);
        ret = fd;
        should_quit = 1;
      }
      goto out;
    }

    ret = ioctl(fd, IHK_OS_ASSIGN_CPU, &req);
    if (ret) {
      ret = -errno;
      dprintf("%s: error: IHK_OS_ASSIGN_CPU returned %d\n",
        __func__, -ret);
      goto out;
    }

   out:
    if (fd != -1) {
      close(fd);
    }
    if (should_quit) goto err;
    should_quit = 1;

    BRANCH_RET_CHK(ret, b_infos[ivec].expected);

    /* check current state */
    n_cpus_assigned_after = ihk_os_get_num_assigned_cpus(index);
    n_cpus_reserved_after = ihk_get_num_reserved_cpus(index);
    if (n_cpus_reserved_after > 0) {
      cpus_reserved_after = calloc(n_cpus_reserved_after, sizeof(int));
      if (!cpus_reserved_after) goto err;
      ret = ihk_query_cpu(index, cpus_reserved_after, n_cpus_reserved_after);
      if (ret) goto err;
    }
    if (n_cpus_assigned_after > 0) {
      cpus_assigned_after = calloc(n_cpus_assigned_after, sizeof(int));
      if (!cpus_assigned_after) goto err;
      ret = ihk_os_query_cpu(index, cpus_assigned_after, n_cpus_assigned_after);
      if (ret) goto err;
    }

    if (ivec == total_branch - 1) {
      OKNG(n_cpus_assigned_after == n_cpus_assigned_prev + num_cpus,
           "check the number of assigned cpus\n");
      OKNG(n_cpus_reserved_after == n_cpus_reserved_prev - num_cpus,
           "check the number of reserved cpus\n");
    } else {
      OKNG(n_cpus_assigned_after == n_cpus_assigned_prev,
           "the number of assigned cpus should be unchanged\n");
      OKNG(n_cpus_reserved_after == n_cpus_reserved_prev,
           "the number of reserved cpus should be unchanged\n");
      if (n_cpus_assigned_prev > 0) {
        OKNG(arr_equals(cpus_assigned_after, cpus_assigned_prev, n_cpus_assigned_prev),
             "list of assigned cpus should be unchanged\n");
      }
      if (n_cpus_reserved_prev > 0) {
        OKNG(arr_equals(cpus_reserved_after, cpus_reserved_prev, n_cpus_reserved_prev),
             "list of reserved cpus should be unchanged\n");
      }
    }
    should_quit = 0;

   err:
    if (cpus_assigned_after) free(cpus_assigned_after);
    if (cpus_reserved_after) free(cpus_reserved_after);
    if (should_quit || ivec == total_branch-1) {
      if (cpus_assigned_prev) free(cpus_assigned_prev);
      if (cpus_reserved_prev) free(cpus_reserved_prev);
      return -EINVAL;
    }
  }

  return 0;
}

int ihk_os_get_num_assigned_cpus_orig(int index)
{
  int ret;
  int fd = -1;

  dprintk("%s: enter\n", __func__);
  if ((fd = ihklib_os_open(index)) < 0) {
    dprintf("%s: error: ihklib_os_open returned %d\n",
      __func__, fd);
    ret = fd;
    goto out;
  }

  ret = ioctl(fd, IHK_OS_GET_NUM_CPUS);
  if (ret < 0) {
    dprintf("%s: error: IHK_OS_GET_NUM_CPUS returned %d\n",
      __func__, ret);
    goto out;
  }

 out:
  if (fd != -1) {
    close(fd);
  }
  return ret;
}

int ihk_os_get_num_assigned_cpus(int index)
{
  if (get_test_mode() != TEST_IHK_OS_GET_NUM_ASSIGNED_CPUS)
    return ihk_os_get_num_assigned_cpus_orig(index);

  int ret = 0;

  dprintk("%s: enter\n", __func__);

  unsigned long ivec = 0;
  unsigned long total_branch = 2;

  branch_info_t b_infos[] = {
    { -EINVAL, "cannot get num_cpus" },
    { 0,       "main case" },
  };

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    int fd = -1;
    int should_quit = 0;
    if ((fd = ihklib_os_open(index)) < 0) {
      dprintf("%s: error: ihklib_os_open returned %d\n",
        __func__, fd);
      ret = fd;
      goto out;
    }

    ret = ioctl(fd, IHK_OS_GET_NUM_CPUS);
    if (ivec == 0 || ret < 0) {
      ret = -EINVAL;
      if (ivec != 0) {
        dprintf("%s: error: IHK_OS_GET_NUM_CPUS returned %d\n",
                __func__, ret);
        should_quit = 1;
      }
      goto out;
    }

   out:
    if (fd != -1) {
      close(fd);
    }
    if (should_quit) return ret;

    BRANCH_RET_CHK(ret, b_infos[ivec].expected);
  }

  return 0;
 err:
  return -EINVAL;
}

int ihk_os_query_cpu_orig(int index, int *cpus, int num_cpus)
{
  int ret;
  struct ihk_ioctl_cpu_desc req = { 0 };
  int fd = -1;

  dprintk("%s: enter\n", __func__);

  ret = ihklib_os_readable(index);
  if (ret) {
    goto out;
  }

  if (num_cpus < 0 || num_cpus > IHK_MAX_NUM_CPUS) {
    dprintf("%s: error: invalid # of cpus (%d)\n",
      __func__, num_cpus);
    ret = -EINVAL;
    goto out;
  }

  if (num_cpus != 0 && cpus == NULL) {
    ret = -EFAULT;
    goto out;
  }

  if ((fd = ihklib_os_open(index)) < 0) {
    dprintf("%s: error: ihklib_os_open\n",
      __func__);
    ret = fd;
    goto out;
  }

  if ((ret = ioctl(fd, IHK_OS_GET_NUM_CPUS)) < 0) {
    ret = -errno;
    dprintf("%s: error: IHK_OS_GET_NUM_CPUS returned %d\n",
      __func__, -ret);
    goto out;
  }

  if (ret != num_cpus) {
    dprintf("%s: error: actual # of CPUs (%d) != requested (%d)\n",
      __func__, ret, num_cpus);
    ret = -EINVAL;
    goto out;
  }
  req.cpus = cpus;
  req.num_cpus = num_cpus;

  if ((ret = ioctl(fd, IHK_OS_QUERY_CPU, &req))) {
    ret = -errno;
    dprintf("%s: error: IHK_OS_QUERY_CPU returned %d\n",
      __func__, -ret);
    goto out;
  }

 out:
  if (fd != -1) {
    close(fd);
  }
  return ret;
}

int ihk_os_query_cpu(int index, int *cpus, int num_cpus)
{
  if (get_test_mode() != TEST_IHK_OS_QUERY_CPU)
    return ihk_os_query_cpu_orig(index, cpus, num_cpus);

  int ret;

  unsigned long ivec = 0;
  unsigned long total_branch = 5;
  int should_quit = 0;
  branch_info_t b_infos[] = {
    { -EINVAL, "cpus is null" },
    { -ENOENT, "ihklib_os_open fail" },
    { -EINVAL, "cannot get #number of assigned cpus" },
    { -EINVAL, "cannot query assigned cpus" },
    { 0,       "main case" },
  };

  dprintk("%s: enter\n", __func__);

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    struct ihk_ioctl_cpu_desc req = { 0 };
    int fd = -1;

    ret = ihklib_os_readable(index);
    if (ret) {
      goto out;
    }

    if (num_cpus < 0 || num_cpus > IHK_MAX_NUM_CPUS) {
      dprintf("%s: error: invalid # of cpus (%d)\n", __func__, num_cpus);
      ret = -EINVAL;
      goto out;
    }

    if (ivec == 0 || (num_cpus != 0 && cpus == NULL)) {
      ret = -EINVAL;
      if (ivec != 0) should_quit = 1;
      goto out;
    }

    fd = ihklib_os_open(index);
    if (ivec == 1 || fd < 0) {
      ret = -ENOENT;
      if (ivec != 1) {
        dprintf("%s: error: ihklib_os_open\n", __func__);
        ret = fd;
        should_quit = 1;
      }
      goto out;
    }

    ret = ioctl(fd, IHK_OS_GET_NUM_CPUS);
    if (ivec == 2 || ret < 0) {
      ret = -EINVAL;
      if (ivec != 2) {
        ret = -errno;
        dprintf("%s: error: IHK_OS_GET_NUM_CPUS returned %d\n", __func__, -ret);
        should_quit = 1;
      }
      goto out;
    }

    if (ret != num_cpus) {
      dprintf("%s: error: actual # of CPUs (%d) != requested (%d)\n",
              __func__, ret, num_cpus);
      ret = -EINVAL;
      goto out;
    }
    req.cpus = cpus;
    req.num_cpus = num_cpus;

    ret = ioctl(fd, IHK_OS_QUERY_CPU, &req);
    if (ivec == 3 || ret) {
      ret = -EINVAL;
      if (ivec != 3) {
        ret = -errno;
        dprintf("%s: error: IHK_OS_QUERY_CPU returned %d\n", __func__, -ret);
        should_quit = 1;
      }
      goto out;
    }

   out:
    if (fd != -1) {
      close(fd);
    }

    if (should_quit) return ret;

    BRANCH_RET_CHK(ret, b_infos[ivec].expected);

    if (ivec == total_branch - 1) {
      struct cpus cpus_online = { 0 };
      int stat = cpus_ls(&cpus_online);
      if (stat) return stat;
      int i, fail = 0;
      for (i = 0; i < num_cpus; i++) {
        if (arr_contains(cpus_online.cpus, cpus_online.ncpus, cpus[i])) {
          fail = 1; break;
        }
      }
      OKNG(!fail, "assigned cpus should be offline\n");
    }
  }

  return ret;
 err:
  return -EINVAL;
}

int ihk_os_release_cpu_orig(int index, int *cpus, int num_cpus)
{
  int ret;
  struct ihk_ioctl_cpu_desc req = { 0 };
  int fd = -1;

  dprintk("%s: enter\n", __func__);

  ret = ihklib_os_readable(index);
  if (ret) {
    goto out;
  }

  if (num_cpus < 0 || num_cpus > IHK_MAX_NUM_CPUS) {
    dprintf("%s: error: invalid # of cpus (%d)\n",
      __func__, num_cpus);
    ret = -EINVAL;
    goto out;
  }

  if (num_cpus != 0 && cpus == NULL) {
    ret = -EFAULT;
    goto out;
  }

  if (num_cpus == 0) {
    ret = 0;
    goto out;
  }
  req.cpus = cpus;
  req.num_cpus = num_cpus;

  if ((fd = ihklib_os_open(index)) < 0) {
    dprintf("%s: error: ihklib_os_open returned %d\n",
      __func__, fd);
    ret = fd;
    goto out;
  }

  ret = ioctl(fd, IHK_OS_RELEASE_CPU, &req);
  if (ret) {
    ret = -errno;
    dprintf("%s: error: IHK_OS_RELEASE_CPU returned %d\n",
      __func__, -ret);
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
  if (get_test_mode() != TEST_IHK_OS_RELEASE_CPU)
    return ihk_os_release_cpu_orig(index, cpus, num_cpus);

  unsigned long ivec = 0;
  unsigned long total_branch = 2;

  branch_info_t b_infos[] = {
    { -ENOENT, "ihklib_os_open fail" },
    { 0,       "main case" },
  };

  /* save previous state */
  int n_cpus_assigned_prev = ihk_os_get_num_assigned_cpus(index);
  int n_cpus_reserved_prev = ihk_get_num_reserved_cpus(index);
  if (n_cpus_assigned_prev < 0 || n_cpus_reserved_prev <= 0) return -EINVAL;
  int *cpus_assigned_prev = NULL, *cpus_reserved_prev = NULL;
  cpus_reserved_prev = calloc(n_cpus_reserved_prev, sizeof(int));
  if (!cpus_reserved_prev) return -ENOMEM;
  int ret = ihk_query_cpu(index, cpus_reserved_prev, n_cpus_reserved_prev);
  if (ret) {
    free(cpus_reserved_prev); return ret;
  }
  if (n_cpus_assigned_prev > 0) {
    cpus_assigned_prev = calloc(n_cpus_assigned_prev, sizeof(int));
    if (!cpus_assigned_prev) {
      free(cpus_reserved_prev); return -ENOMEM;
    }
    ret = ihk_os_query_cpu(index, cpus_assigned_prev, n_cpus_assigned_prev);
    if (ret) {
      free(cpus_reserved_prev);
      free(cpus_assigned_prev);
      return ret;
    }
  }

  dprintk("%s: enter\n", __func__);

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    struct ihk_ioctl_cpu_desc req = { 0 };
    int fd = -1;
    int should_quit = 0;
    int n_cpus_assigned_after = 0, n_cpus_reserved_after = 0;
    int *cpus_assigned_after = NULL, *cpus_reserved_after = NULL;

    ret = ihklib_os_readable(index);
    if (ret) {
      goto out;
    }

    if (num_cpus < 0 || num_cpus > IHK_MAX_NUM_CPUS) {
      dprintf("%s: error: invalid # of cpus (%d)\n",
        __func__, num_cpus);
      ret = -EINVAL;
      goto out;
    }

    if (num_cpus != 0 && cpus == NULL) {
      ret = -EFAULT;
      goto out;
    }

    if (num_cpus == 0) {
      ret = 0;
      goto out;
    }
    req.cpus = cpus;
    req.num_cpus = num_cpus;

    fd = ihklib_os_open(index);
    if (ivec == 0 || fd < 0) {
      ret = -ENOENT;
      if (ivec != 0) {
        dprintf("%s: error: ihklib_os_open returned %d\n", __func__, fd);
        should_quit = 1;
      }
      goto out;
    }

    ret = ioctl(fd, IHK_OS_RELEASE_CPU, &req);
    if (ret) {
      ret = -errno;
      dprintf("%s: error: IHK_OS_RELEASE_CPU returned %d\n",
        __func__, -ret);
      goto out;
    }

   out:
    if (fd != -1) {
      close(fd);
    }

    if (should_quit) goto err;
    should_quit = 1;

    BRANCH_RET_CHK(ret, b_infos[ivec].expected);

    /* check current state */
    n_cpus_assigned_after = ihk_os_get_num_assigned_cpus(index);
    n_cpus_reserved_after = ihk_get_num_reserved_cpus(index);
    if (n_cpus_reserved_after > 0) {
      cpus_reserved_after = calloc(n_cpus_reserved_after, sizeof(int));
      if (!cpus_reserved_after) goto err;
      ret = ihk_query_cpu(index, cpus_reserved_after, n_cpus_reserved_after);
      if (ret) goto err;
    }
    if (n_cpus_assigned_after > 0) {
      cpus_assigned_after = calloc(n_cpus_assigned_after, sizeof(int));
      if (!cpus_assigned_after) goto err;
      ret = ihk_os_query_cpu(index, cpus_assigned_after, n_cpus_assigned_after);
      if (ret) goto err;
    }

    if (ivec == total_branch - 1) {
      OKNG(n_cpus_assigned_after == 0,
           "all assigned cpus should be released\n");
      OKNG(n_cpus_reserved_after == n_cpus_reserved_prev + num_cpus,
           "check the number of reserved cpus\n");
    } else {
      OKNG(n_cpus_assigned_after == n_cpus_assigned_prev,
           "the number of assigned cpus should be unchanged\n");
      OKNG(n_cpus_reserved_after == n_cpus_reserved_prev,
           "the number of reserved cpus should be unchanged\n");
      if (n_cpus_assigned_prev > 0) {
        OKNG(arr_equals(cpus_assigned_after, cpus_assigned_prev, n_cpus_assigned_prev),
             "list of assigned cpus should be unchanged\n");
      }
      if (n_cpus_reserved_prev > 0) {
        OKNG(arr_equals(cpus_reserved_after, cpus_reserved_prev, n_cpus_reserved_prev),
             "list of reserved cpus should be unchanged\n");
      }
    }
    should_quit = 0;

   err:
    if (cpus_assigned_after) free(cpus_assigned_after);
    if (cpus_reserved_after) free(cpus_reserved_after);
    if (should_quit || ivec == total_branch-1) {
      if (cpus_assigned_prev) free(cpus_assigned_prev);
      if (cpus_reserved_prev) free(cpus_reserved_prev);
      return -EINVAL;
    }
  }

  return 0;
}

int ihk_os_set_ikc_map_orig(int index, struct ihk_ikc_cpu_map *map, int num_cpus)
{
  int ret, i;
  struct ihk_ioctl_ikc_desc req = { 0 };
  int fd = -1;

  dprintk("%s: enter\n", __func__);

  ret = ihklib_os_readable(index);
  if (ret) {
    goto out;
  }

  if (num_cpus < 0 || num_cpus > IHK_MAX_NUM_CPUS) {
    dprintf("%s: error: invalid # of cpus (%d)\n", __func__, num_cpus);
    ret = -EINVAL;
    goto out;
  }

  if (num_cpus != 0 && map == NULL) {
    ret = -EFAULT;
    goto out;
  }

  ret = ihk_os_get_num_assigned_cpus(index);
  if (ret != num_cpus) {
    dprintf("%s: error: actual number of CPUs (%d) is"
            " different than requested (%d)\n",
            __func__, ret, num_cpus);
    ret = -EINVAL;
    goto out;
  }

  req.src_cpus = calloc(num_cpus, sizeof(int));
  if (!req.src_cpus) {
    dprintf("%s: error: allocating request src_cpus\n", __func__);
    ret = -ENOMEM;
    goto out;
  }

  req.dst_cpus = calloc(num_cpus, sizeof(int));
  if (!req.dst_cpus) {
    dprintf("%s: error: allocating request dst_cpuss\n", __func__);
    ret = -ENOMEM;
    goto out;
  }

  for (i = 0; i < num_cpus; i++) {
    req.src_cpus[i] = map[i].src_cpu;
    req.dst_cpus[i] = map[i].dst_cpu;
  }
  req.num_cpus = num_cpus;

  if ((fd = ihklib_os_open(index)) < 0) {
    dprintf("%s: error: ihklib_os_open\n", __func__);
    ret = fd;
    goto out;
  }

  ret = ioctl(fd, IHK_OS_SET_IKC_MAP, &req);
  if (ret) {
    ret = -errno;
    dprintf("%s: IHK_OS_SET_IKC_MAP returned %d\n", __func__, -ret);
    goto out;
  }

 out:
  if (fd != -1) {
    close(fd);
  }
  free(req.src_cpus);
  free(req.dst_cpus);
  return ret;
}

int ihk_os_set_ikc_map(int index, struct ihk_ikc_cpu_map *map, int num_cpus)
{
  if (get_test_mode() != TEST_IHK_OS_SET_IKC_MAP)
    return ihk_os_set_ikc_map_orig(index, map, num_cpus);

  unsigned long ivec = 0;
  unsigned long total_branch = 4;

  branch_info_t b_infos[] = {
    { -EINVAL, "num_cpus is zero" },
    { -EINVAL, "map is null" },
    { -ENOENT, "ihklib_os_open fail" },
    { 0,       "main case" },
  };

  dprintk("%s: enter\n", __func__);

  int ret, i;

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    struct ihk_ioctl_ikc_desc req = { 0 };
    struct ikc_cpu_map map_expected = { 0 };
    int fd = -1;
    int should_quit = 0;

    ret = ihklib_os_readable(index);
    if (ret) {
      goto out;
    }

    if (num_cpus < 0 || num_cpus > IHK_MAX_NUM_CPUS) {
      dprintf("%s: error: invalid # of cpus (%d)\n", __func__, num_cpus);
      ret = -EINVAL;
      goto out;
    }

    if (ivec == 0 || num_cpus == 0) {
      ret = -EINVAL;
      if (ivec != 0) return ret;
      goto out;
    }

    if (ivec == 1 || (num_cpus != 0 && map == NULL)) {
      ret = -EINVAL;
      if (ivec != 1) return ret;
      goto out;
    }

    ret = ihk_os_get_num_assigned_cpus(index);
    if (ret != num_cpus) {
      dprintf("%s: error: actual number of CPUs (%d) is"
              " different than requested (%d)\n",
              __func__, ret, num_cpus);
      ret = -EINVAL;
      goto out;
    }

    req.src_cpus = calloc(num_cpus, sizeof(int));
    if (!req.src_cpus) {
      dprintf("%s: error: allocating request src_cpus\n", __func__);
      ret = -ENOMEM;
      goto out;
    }

    req.dst_cpus = calloc(num_cpus, sizeof(int));
    if (!req.dst_cpus) {
      dprintf("%s: error: allocating request dst_cpuss\n", __func__);
      ret = -ENOMEM;
      goto out;
    }

    for (i = 0; i < num_cpus; i++) {
      req.src_cpus[i] = map[i].src_cpu;
      req.dst_cpus[i] = map[i].dst_cpu;
    }
    req.num_cpus = num_cpus;

    fd = ihklib_os_open(index);
    if (ivec == 2 || fd < 0) {
      ret = -ENOENT;
      if (ivec != 2) {
        dprintf("%s: error: ihklib_os_open\n", __func__);
        should_quit = 1;
      }
      goto out;
    }

    ret = ioctl(fd, IHK_OS_SET_IKC_MAP, &req);
    if (ret) {
      ret = -errno;
      dprintf("%s: IHK_OS_SET_IKC_MAP returned %d\n", __func__, -ret);
      goto out;
    }

   out:
    if (fd != -1) {
      close(fd);
    }
    free(req.src_cpus);
    free(req.dst_cpus);
    if (should_quit) return ret;

    BRANCH_RET_CHK(ret, b_infos[ivec].expected);

    map_expected.map = map;
    map_expected.ncpus = num_cpus;

    if (ivec == total_branch - 1) {
      ret = ikc_cpu_map_check(&map_expected);
      OKNG(ret == 0, "ikc map configured as expected\n");
      /* check active IKC channels */
      {
        // prepare environment
        ret = os_load();
        OKNG(ret == 0, "load os success\n");
        ret = os_kargs();
        OKNG(ret == 0, "pass kargs success\n");
        ret = ihk_os_boot(0);
        OKNG(ret == 0, "boot os success\n");
      }
      ret = ikc_cpu_map_check_channels(num_cpus);
      OKNG(ret == 0, "all IKC channels are active\n");
      {
        // cleanup environment
        ret = linux_kill_mcexec();
        OKNG(ret == 0, "kill mcexec success\n");
        ret = ihk_os_shutdown(0);
        OKNG(ret == 0, "shutdown os success\n");
        os_wait_for_status(IHK_STATUS_INACTIVE);
      }
    } else {
      ret = ikc_cpu_map_check(&map_expected);
      OKNG(ret < 0, "ikc map has not configured successfully\n");
    }
  }

  return 0;
 err:
  return -EINVAL;
}

int ihk_os_get_ikc_map_orig(int index, struct ihk_ikc_cpu_map *map, int num_cpus)
{
  int ret, i;
  struct ihk_ioctl_ikc_desc req = { 0 };
  int fd = -1;

  dprintk("%s: enter\n", __func__);

  ret = ihklib_os_readable(index);
  if (ret) {
    goto out;
  }

  if (num_cpus < 0 || num_cpus > IHK_MAX_NUM_CPUS) {
    dprintf("%s: error: invalid # of cpus (%d)\n",
            __func__, num_cpus);
    ret = -EINVAL;
    goto out;
  }

  if (num_cpus != 0 && map == NULL) {
    ret = -EFAULT;
    goto out;
  }

  ret = ihk_os_get_num_assigned_cpus(index);
  if (ret != num_cpus) {
    dprintf("%s: error: actual number of CPUs (%d) is"
            " different than requested (%d)\n",
            __func__, ret, num_cpus);
    ret = -EINVAL;
    goto out;
  }

  req.src_cpus = calloc(num_cpus, sizeof(int));
  if (!req.src_cpus) {
    dprintf("%s: error: allocating request src_cpus\n", __func__);
    ret = -ENOMEM;
    goto out;
  }

  req.dst_cpus = calloc(num_cpus, sizeof(int));
  if (!req.dst_cpus) {
    dprintf("%s: error: allocating request dst_cpuss\n", __func__);
    ret = -ENOMEM;
    goto out;
  }

  req.num_cpus = num_cpus;

  if ((fd = ihklib_os_open(index)) < 0) {
    dprintf("%s: error: ihklib_os_open\n", __func__);
    ret = fd;
    goto out;
  }

  ret = ioctl(fd, IHK_OS_GET_IKC_MAP, &req);
  if (ret) {
    ret = -errno;
    dprintf("%s: IHK_OS_GET_IKC_MAP returned %d\n", __func__, -ret);
    goto out;
  }

  for (i = 0; i < req.num_cpus; i++) {
    map[i].src_cpu = req.src_cpus[i];
    map[i].dst_cpu = req.dst_cpus[i];
  }

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
  if (get_test_mode() != TEST_IHK_OS_GET_IKC_MAP)
    return ihk_os_get_ikc_map_orig(index, map, num_cpus);

  int ret, i;

  unsigned long ivec = 0;
  unsigned long total_branch = 5;
  int should_quit = 0;
  
  branch_info_t b_infos[] = {
    { -EINVAL, "map is null" },
    { -EINVAL, "num_cpus is zero" },
    { -ENOENT, "ihklib_os_open fail" },
    { -EINVAL, "get ikc map fail" },
    { 0,       "main case" },
  };

  dprintk("%s: enter\n", __func__);

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    struct ihk_ioctl_ikc_desc req = { 0 };
    int fd = -1;

    ret = ihklib_os_readable(index);
    if (ret) {
      goto out;
    }

    if (num_cpus < 0 || num_cpus > IHK_MAX_NUM_CPUS) {
      dprintf("%s: error: invalid # of cpus (%d)\n",
              __func__, num_cpus);
      ret = -EINVAL;
      goto out;
    }

    if (ivec == 0 || num_cpus != 0 && map == NULL) {
      ret = -EINVAL;
      if (ivec != 0) should_quit = 1;
      goto out;
    }

    if (ivec == 1 || num_cpus == 0) {
      ret = -EINVAL;
      if (ivec != 1) should_quit = 1;
      goto out;
    }

    ret = ihk_os_get_num_assigned_cpus(index);
    if (ret != num_cpus) {
      dprintf("%s: error: actual number of CPUs (%d) is"
              " different than requested (%d)\n",
              __func__, ret, num_cpus);
      ret = -EINVAL;
      goto out;
    }

    req.src_cpus = calloc(num_cpus, sizeof(int));
    if (!req.src_cpus) {
      dprintf("%s: error: allocating request src_cpus\n", __func__);
      ret = -ENOMEM;
      goto out;
    }

    req.dst_cpus = calloc(num_cpus, sizeof(int));
    if (!req.dst_cpus) {
      dprintf("%s: error: allocating request dst_cpuss\n", __func__);
      ret = -ENOMEM;
      goto out;
    }

    req.num_cpus = num_cpus;

    fd = ihklib_os_open(index);
    if (ivec == 2 || fd < 0) {
      ret = -ENOENT;
      if (ivec != 2) {
        dprintf("%s: error: ihklib_os_open\n", __func__);
        should_quit = 1;
      }
      goto out;
    }

    ret = ioctl(fd, IHK_OS_GET_IKC_MAP, &req);
    if (ivec == 3 || ret) {
      ret = -EINVAL;
      if (ivec != 3) {
        ret = -errno;
        dprintf("%s: IHK_OS_GET_IKC_MAP returned %d\n", __func__, -ret);
        should_quit = 1;
      }
      goto out;
    }

    for (i = 0; i < req.num_cpus; i++) {
      map[i].src_cpu = req.src_cpus[i];
      map[i].dst_cpu = req.dst_cpus[i];
    }

    ret = 0;

   out:
    if (fd != -1) {
      close(fd);
    }
    if (req.src_cpus) free(req.src_cpus);
    if (req.dst_cpus) free(req.dst_cpus);
    if (should_quit) return ret;

    BRANCH_RET_CHK(ret, b_infos[ivec].expected);
  }

  return 0;
 err:
  return -EINVAL;
}

int ihk_os_assign_mem(int index, struct ihk_mem_chunk *mem_chunks, int num_mem_chunks)
{
  int ret, i;
  struct ihk_mem_req req = { 0 };
  int fd = -1;

  dprintk("%s: enter\n", __func__);

  ret = ihklib_os_readable(index);
  if (ret) {
    goto out;
  }

  if (num_mem_chunks < 0 || num_mem_chunks > IHK_MAX_NUM_MEM_CHUNKS) {
    dprintf("%s: error: invalid # of chunks (%d)\n",
      __func__, num_mem_chunks);
    ret = -EINVAL;
    goto out;
  }

  if (num_mem_chunks != 0 && mem_chunks == NULL) {
    ret = -EFAULT;
    goto out;
  }

  if (num_mem_chunks == 0) {
    ret = 0;
    goto out;
  };

  req.sizes = calloc(num_mem_chunks, sizeof(size_t));
  if (!req.sizes) {
    dprintf("%s: error: allocating request sizes\n",
      __func__);
    ret = -ENOMEM;
    goto out;
  }

  req.numa_ids = calloc(num_mem_chunks, sizeof(int));
  if (!req.numa_ids) {
    dprintf("%s: error: allocating request numa_ids\n",
      __func__);
    ret = -ENOMEM;
    goto out;
  }

  for (i = 0; i < num_mem_chunks; i++) {
    req.sizes[i] = (size_t)mem_chunks[i].size;
    req.numa_ids[i] = mem_chunks[i].numa_node_number;
  }
  req.num_chunks = num_mem_chunks;

  if ((fd = ihklib_os_open(index)) < 0) {
    dprintf("%s: error: ihklib_os_open returned %d\n",
      __func__, fd);
    ret = fd;
    goto out;
  }

  ret = ioctl(fd, IHK_OS_ASSIGN_MEM, &req);
  if (ret) {
    ret = -errno;
    dprintf("%s: IHK_OS_ASSIGN_MEM returned %d\n",
      __func__, -ret);
    goto out;
  }

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
  int ret;
  struct ihk_mem_req req = { 0 };
  int fd = -1;

  dprintk("%s: enter\n", __func__);
  if ((fd = ihklib_os_open(index)) < 0) {
    dprintf("%s: error: ihklib_os_open\n",
      __func__);
    ret = fd;
    goto out;
  }

  req.num_chunks = 0;   /* means only get num_chunks */

  ret = ioctl(fd, IHK_OS_QUERY_MEM, &req);
  if (ret) {
    ret = -errno;
    dprintf("%s: IHK_OS_QUERY_MEM returned %d\n",
      __func__, -ret);
    goto out;
  }

  ret = req.num_chunks;

 out:
  if (fd != -1) {
    close(fd);
  }
  return ret;
}

int ihk_os_query_mem(int index, struct ihk_mem_chunk *mem_chunks,
         int _num_mem_chunks)
{
  int ret, i;
  int num_mem_chunks;
  struct ihk_mem_req req = { 0 };
  int fd = -1;

  dprintk("%s: enter\n", __func__);
  ret = ihklib_os_readable(index);
  if (ret) {
    goto out;
  }

  if (_num_mem_chunks < 0 || _num_mem_chunks > IHK_MAX_NUM_MEM_CHUNKS) {
    dprintf("%s: error: invalid # of chunks (%d)\n",
      __func__, _num_mem_chunks);
    ret = -EINVAL;
    goto out;
  }

  if (_num_mem_chunks != 0 && mem_chunks == NULL) {
    ret = -EFAULT;
    goto out;
  }

  ret = ihk_os_get_num_assigned_mem_chunks(index);
  if (ret < 0) {
    dprintf("%s: error: ihk_os_get_num_assigned_mem_chunks"
      " returned %d\n",
      __func__, ret);
    goto out;
  }
  num_mem_chunks = ret;

  if (_num_mem_chunks != num_mem_chunks) {
    dprintf("%s: error: actual # of chunks (%d) !="
      " requested (%d)\n",
      __func__, num_mem_chunks, _num_mem_chunks);
    ret = -EINVAL;
    goto out;
  }

  req.sizes = calloc(num_mem_chunks, sizeof(size_t));
  if (!req.sizes) {
    dprintf("%s: error: allocating request sizes\n",
      __func__);
    ret = -ENOMEM;
    goto out;
  }

  req.numa_ids = calloc(num_mem_chunks, sizeof(int));
  if (!req.numa_ids) {
    dprintf("%s: error: allocating request numa_ids\n",
      __func__);
    ret = -ENOMEM;
    goto out;
  }

  req.num_chunks = num_mem_chunks;

  if ((fd = ihklib_os_open(index)) < 0) {
    dprintf("%s: error: ihklib_os_open\n",
      __func__);
    ret = fd;
    goto out;
  }

  ret = ioctl(fd, IHK_OS_QUERY_MEM, &req);
  if (ret) {
    ret = -errno;
    dprintf("%s: error: IHK_OS_QUERY_MEM returned %d\n",
      __func__, -ret);
    goto out;
  }

  for (i = 0; i < num_mem_chunks; i++) {
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

int ihk_os_release_mem(int index, struct ihk_mem_chunk *mem_chunks,
    int num_mem_chunks)
{
  int ret, i;
  struct ihk_mem_req req = { 0 };
  int fd = -1;
  struct ihk_mem_chunk *query_mem_chunks = NULL;

  dprintk("%s: enter\n", __func__);

  ret = ihklib_os_readable(index);
  if (ret) {
    goto out;
  }

  if (num_mem_chunks < 0 || num_mem_chunks > IHK_MAX_NUM_MEM_CHUNKS) {
    dprintf("%s: error: invalid # of chunks (%d)\n",
      __func__, num_mem_chunks);
    ret = -EINVAL;
    goto out;
  }

  if (num_mem_chunks != 0 && mem_chunks == NULL) {
    ret = -EFAULT;
    goto out;
  }

  if (num_mem_chunks == 0) {
    ret = 0;
    goto out;
  };

  if (mem_chunks[0].size == IHK_SMP_MEM_ALL) {
    /* Special case for releasing all memory */
    num_mem_chunks = ihk_os_get_num_assigned_mem_chunks(index);
    query_mem_chunks = calloc(num_mem_chunks,
            sizeof(struct ihk_mem_chunk));
    if (query_mem_chunks == NULL) {
      dprintf("%s: error: allocating memory chunks\n",
        __func__);
      ret = -ENOMEM;
      goto out;
    }

    ret = ihk_os_query_mem(index, query_mem_chunks, num_mem_chunks);
    if (ret) {
      dprintf("%s: error: ihk_os_query_mem returned %d\n",
        __func__, ret);
      goto out;
    }

    mem_chunks = query_mem_chunks;
  }

  req.sizes = calloc(num_mem_chunks, sizeof(size_t));
  if (!req.sizes) {
    dprintf("%s: error: allocating request sizes\n",
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

  if ((fd = ihklib_os_open(index)) < 0) {
    eprintf("%s: error: ihklib_os_open\n",
      __func__);
    ret = fd;
    goto out;
  }

  ret = ioctl(fd, IHK_OS_RELEASE_MEM, &req);
  if (ret) {
    ret = -errno;
    dprintf("%s: error: IHK_OS_RELEASE_MEM returned %d\n",
      __func__, -ret);
    goto out;
  }

 out:
  if (fd != -1) {
    close(fd);
  }
  free(query_mem_chunks);
  free(req.sizes);
  free(req.numa_ids);
  return ret;
}

int ihk_os_get_eventfd(int index, int type)
{
  int fd = -1;
  int ret;
  struct ihk_os_ioctl_eventfd_desc desc;

  dprintk("%s: enter\n", __func__);
  memset(&desc, 0, sizeof(desc));

  if ((fd = ihklib_os_open(index)) < 0) {
    dprintf("%s: error: ihklib_os_open\n",
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
    dprintf("%s: error: unknown type: %d\n",
      __func__, type);
    ret = -EINVAL;
    goto out;
  }

  desc.fd = eventfd(0, 0);
  desc.type = type;

  ret = ioctl(fd, IHK_OS_REGISTER_EVENT, &desc);
  if (ret) {
    ret = -errno;
    dprintf("%s: error: IHK_OS_REGISTER_EVENT returned %d\n",
      __func__, -ret);
    goto out;
  }

  ret = desc.fd;
 out:
  if (fd != -1) {
    close(fd);
  }
  dprintk("%s: returning %d\n", __func__, ret);
  return ret;
}

int ihk_os_load(int index, char* fn)
{
  int ret;
  int fd = -1;

  dprintk("%s: enter\n", __func__);
  if ((fd = ihklib_os_open(index)) < 0) {
    dprintf("%s: error: ihklib_os_open\n",
      __func__);
    ret = fd;
    goto out;
  }

  if (fn == NULL) {
    dprintf("%s: error: file name is NULL\n",
      __func__);
    ret = -EINVAL;
    goto out;
  }

  ret = ioctl(fd, IHK_OS_LOAD, (unsigned long)fn);
  if (ret) {
    ret = -errno;
    dprintf("%s: error: IHK_OS_LOAD returned %d\n",
      __func__, -ret);
    goto out;
  }

 out:
  if (fd != -1) {
    close(fd);
  }
  return ret;
}

int ihk_os_kargs(int index, char* kargs)
{
  int ret;
  int fd = -1;

  dprintk("%s: enter\n", __func__);
  if (kargs == NULL) {
    dprintf("%s: warning: kargs is NULL\n",
      __func__);
    ret = -EFAULT;
    goto out;
  }

  if ((fd = ihklib_os_open(index)) < 0) {
    dprintf("%s: error: ihklib_os_open\n",
      __func__);
    ret = fd;
    goto out;
  }

  ret = ioctl(fd, IHK_OS_SET_KARGS, kargs);
  if (ret) {
    ret = -errno;
    dprintf("%s: error: IHK_OS_SET_KARGS returned %d\n",
      __func__, -ret);
    goto out;
  }

 out:
  if (fd != -1) {
    close(fd);
  }
  return ret;
}

int ihk_os_boot(int index)
{
  int ret;
  int fd = -1;
  int i;

  dprintk("%s: enter\n", __func__);
  if ((fd = ihklib_os_open(index)) < 0) {
    dprintf("%s: error: ihklib_os_open returned %d\n",
      __func__, fd);
    ret = fd;
    goto out;
  }

  if ((ret = ioctl(fd, IHK_OS_BOOT, 0)) == -1) {
    ret = -errno;
    dprintf("%s: error: IHK_OS_BOOT returned %d\n",
      __func__, -ret);
    goto out;
  }

  for (i = 0; i < 50; i++) { /* 10 second */
    ret = ioctl(fd, IHK_OS_STATUS);

    switch (ret) {
    case IHK_OS_STATUS_BOOTING:
    case IHK_OS_STATUS_BOOTED:
    case IHK_OS_STATUS_READY:
      usleep(200000);
      continue;
    default:
      break;
    }
  }

  if (ret == -1) {
    ret = -errno;
    dprintf("%s: error: IHK_OS_STATUS returned %d\n",
      __func__, -ret);
    goto out;
  }

  if (ret != IHK_OS_STATUS_RUNNING) {
    dprintf("%s: error: "
      "status didn't change to RUNNING (%d)\n",
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
  int ret;
  int fd = -1;

  dprintk("%s: enter\n", __func__);

  if ((fd = ihklib_os_open(index)) < 0) {
    eprintf("%s: error: ihklib_os_open\n",
      __func__);
    ret = fd;
    goto out;
  }

  ret = ioctl(fd, IHK_OS_SHUTDOWN, 0);
  if (ret) {
    ret = -errno;
    dprintf("%s: IHK_OS_SHUTDOWN returned %d\n",
      __func__, -ret);
    goto out;
  }
 out:
  if (fd != -1) {
    close(fd);
  }
  return ret;

}

int ihk_os_get_status(int index)
{
  int ret;
  int fd = -1;

  dprintk("%s: enter\n", __func__);

  if ((fd = ihklib_os_open(index)) < 0) {
    dprintf("%s: error: ihklib_os_open\n",
      __func__);
    ret = fd;
    goto out;
  }

  ret = ioctl(fd, IHK_OS_STATUS);
  if (ret < 0) {
    dprintf("%s: error: IHK_OS_STATUS: %d\n",
      __func__, ret);
    goto out;
  }

  switch (ret) {
  case IHK_OS_STATUS_NOT_BOOTED: /* before smp_ihk_os_boot or
          * after smp_ihk_destroy_os
          */
    ret = IHK_STATUS_INACTIVE;
    break;
  case IHK_OS_STATUS_BOOTING:  /* smp_ihk_os_boot -- arch_init */
  case IHK_OS_STATUS_BOOTED:  /* arch_init -- arch_ready */
  case IHK_OS_STATUS_READY:  /* arch_ready -- done_init */
    ret = IHK_STATUS_BOOTING;
    break;
  case IHK_OS_STATUS_RUNNING:  /* after done_init */
    ret = IHK_STATUS_RUNNING;
    break;
  case IHK_OS_STATUS_SHUTDOWN:  /* smp_ihk_os_shutdown --
           * smp_ihk_destroy_os
           */
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
    dprintf("%s: error: unknown os status: %d\n",
      __func__, ret);
    ret = -EINVAL;
    goto out;
  }

 out:
  if (fd != -1) {
    close(fd);
  }
  dprintk("%s: returning %d\n", __func__, ret);
  return ret;
}

int ihk_os_get_kmsg_size(int index)
{
  int ret;
  int fd = -1;

  dprintk("%s: enter\n", __func__);

  if ((fd = ihklib_os_open(index)) < 0) {
    dprintf("%s: error: ihklib_os_open\n",
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
  int ret;
  int fd = -1;

  dprintk("%s: enter\n", __func__);

  if ((fd = ihklib_os_open(index)) < 0) {
    dprintf("%s: error: ihklib_os_open returned %d\n",
      __func__, fd);
    ret = fd;
    goto out;
  }

  if (sz_kmsg != IHK_KMSG_SIZE) {
    dprintf("%s: error: invalid buffer size\n",
      __func__);
    ret = -EINVAL;
    goto out;
  }

  if (kmsg == NULL) {
    dprintf("%s: error: invalid buffer address\n",
      __func__);
    ret = -EFAULT;
    goto out;
  }

  ret = ioctl(fd, IHK_OS_READ_KMSG, (unsigned long)kmsg);
  if (ret < 0) {
    ret = -errno;
    dprintf("%s: IHK_OS_READ_KMSG returned %d\n",
      __func__, -ret);
    goto out;
  }

 out:
  if (fd != -1) {
    close(fd);
  }
  return ret;
}

int ihk_os_clear_kmsg(int index)
{
  int ret;
  int fd = -1;

  dprintk("%s: enter\n", __func__);

  if ((fd = ihklib_os_open(index)) < 0) {
    dprintf("%s: error: ihklib_os_open\n",
      __func__);
    ret = fd;
    goto out;
  }

  ret = ioctl(fd, IHK_OS_CLEAR_KMSG, 0);
  if (ret) {
    ret = -errno;
    dprintf("%s: IHK_OS_CLEAR_KMSG returned %d\n",
      __func__, -ret);
    goto out;
  }

 out:
  if (fd != -1) {
    close(fd);
  }
  return ret;
}

int ihk_os_get_num_numa_nodes(int index)
{
  int ret;
  int fd = -1;

  dprintk("%s: enter\n", __func__);

  if ((fd = ihklib_os_open(index)) < 0) {
    dprintf("%s: error: ihklib_os_open\n",
      __func__);
    ret = fd;
    goto out;
  }

  ret = ioctl(fd, IHK_OS_GET_NUM_NUMA_NODES);
  if (ret < 0) {
    ret = -errno;
    dprintf("%s: IHK_OS_GET_NUM_NUMA_NODES returned %d\n",
      __func__, -ret);
    goto out;
  }

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

  dprintk("%s: enter\n", __func__);

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

  dprintk("%s: enter\n", __func__);

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
  dprintk("%s: enter\n", __func__);
  return ihklib_os_query_mem(index, result, num_numa_nodes,
           IHKLIB_OS_QUERY_MEM_TOTAL);
}

int ihk_os_query_free_mem(int index, unsigned long *result,
          int num_numa_nodes)
{
  dprintk("%s: enter\n", __func__);
  return ihklib_os_query_mem(index, result, num_numa_nodes,
           IHKLIB_OS_QUERY_MEM_FREE);
}

int ihk_os_get_num_pagesizes(int index)
{
  int ret;
  int fd = -1;

  dprintk("%s: enter\n", __func__);

  if ((fd = ihklib_os_open(index)) < 0) {
    dprintf("%s: error: ihklib_os_open returned %d\n",
      __func__, fd);
    ret = fd;
    goto out;
  }

  ret = IHK_MAX_NUM_PGSIZES;

 out:
  if (fd != -1) {
    close(fd);
  }
  dprintk("%s: returning %d\n", __func__, ret);
  return ret;
}

int ihk_os_get_pagesizes(int index, long *pgsizes, int num_pgsizes)
{
  int ret;
  int i;
  int fd = -1;

  dprintk("%s: enter\n", __func__);

  if ((fd = ihklib_os_open(index)) < 0) {
    dprintf("%s: error: ihklib_os_open\n",
      __func__);
    ret = fd;
    goto out;
  }

  if (!pgsizes) {
    ret = -EFAULT;
    goto out;
  }

  if (num_pgsizes != IHK_MAX_NUM_PGSIZES) {
    ret = -EINVAL;
    goto out;
  }

  for (i = 0; i < num_pgsizes; i++) {
    pgsizes[i] = rusage_pgtype_to_pgsize((enum ihk_os_pgsize)i);
  }

  ret = 0;
 out:
  if (fd != -1) {
    close(fd);
  }
  dprintk("%s: returning %d\n", __func__, ret);
  return ret;
}

#ifdef ENABLE_RUSAGE
int ihk_os_getrusage(int index, struct ihk_os_rusage *rusage,
         size_t size_rusage)
{
  int ret;
  int fd = -1;

  dprintk("%s: enter\n", __func__);

  if (!rusage) {
    dprintf("%s: error: output buffer is NULL\n",
      __func__);
    ret = -EFAULT;
    goto out;
  }

  if (size_rusage != sizeof(struct ihk_os_rusage)) {
    dprintf("%s: error: size of output buffer is invalid\n",
      __func__);
    ret = -EINVAL;
    goto out;
  }

  struct mcctrl_ioctl_getrusage_desc desc = {
    .rusage = rusage,
    .size_rusage = size_rusage,
  };

  if ((fd = ihklib_os_open(index)) < 0) {
    dprintf("%s: error: ihklib_os_open returned %d\n",
      __func__, fd);
    ret = fd;
    goto out;
  }

  ret = ioctl(fd, IHK_OS_GETRUSAGE, &desc);
  if (ret) {
    ret = -errno;
    dprintf("%s: IHK_OS_GETRUSAGE returned %d\n",
      __func__, -ret);
    goto out;
  }

 out:
  if (fd != -1) {
    close(fd);
  }
  dprintk("%s: returning %d\n", __func__, ret);
  return ret;
}
#else
int ihk_os_getrusage(int index, struct ihk_os_rusage *rusage,
         size_t size_rusage)
{
  dprintf("Specify --enable-rusage when configuring.\n");
  return -ENOSYS;
}
#endif

int ihk_os_setperfevent(int index, ihk_perf_event_attr *attr, int n)
{
  int ret;
  int fd = -1;

  dprintk("%s: enter\n", __func__);

  if ((fd = ihklib_os_open(index)) < 0) {
    dprintf("%s: error: ihklib_os_open returned %d\n",
      __func__, fd);
    ret = fd;
    goto out;
  }

  if (n <= 0) {
    dprintf("%s: invalid number(%d) of events\n",
      __func__, n);
    ret = -EINVAL;
    goto out;
  }

  ret = ioctl(fd, IHK_OS_AUX_PERF_NUM, n);
  if (ret) {
    ret = -errno;
    dprintf("%s: IHK_OS_AUX_PERF_NUM returned %d\n",
      __func__, -ret);
    goto out;
  }

  ret = ioctl(fd, IHK_OS_AUX_PERF_SET, attr);
  if (ret < 0) {
    ret = -errno;
    dprintf("%s: IHK_OS_AUX_PERF_SET returned %d\n",
      __func__, -ret);
    goto out;
  }

 out:
  if (fd != -1) {
    close(fd);
  }
  dprintk("%s: returning %d\n", __func__, ret);
  return ret;
}

int ihk_os_perfctl(int index, int comm)
{
  int ret;
  int fd = -1;

  dprintk("%s: enter\n", __func__);
  if ((fd = ihklib_os_open(index)) < 0) {
    dprintf("%s: error: ihklib_os_open returned %d\n",
      __func__, fd);
    ret = fd;
    goto out;
  }

  switch (comm) {
  case PERF_EVENT_ENABLE: /* start PA event */
    ret = ioctl(fd, IHK_OS_AUX_PERF_ENABLE, 0);
    break;
  case PERF_EVENT_DISABLE: /* stop PA event */
    ret = ioctl(fd, IHK_OS_AUX_PERF_DISABLE, 0);
    break;
  case PERF_EVENT_DESTROY: /* stop PA event and
          * reset # of counters
          */
    ret = ioctl(fd, IHK_OS_AUX_PERF_DESTROY, 0);
    break;
  default:
    ret = -EINVAL;
    goto out;
  }
  if (ret) {
    ret = -errno;
    dprintf("%s: IHK_OS_AUX_PERF_* returned %d\n",
      __func__, -ret);
    goto out;
  }

 out:
  if (fd != -1) {
    close(fd);
  }
  dprintk("%s: returning %d\n", __func__, ret);
  return ret;
}

int ihk_os_getperfevent(int index, unsigned long *counter, int n)
{
  int ret;
  int fd = -1;

  dprintk("%s: enter\n", __func__);
  if ((fd = ihklib_os_open(index)) < 0) {
    dprintf("%s: error: ihklib_os_open returned %d\n",
      __func__, fd);
    ret = fd;
    goto out;
  }

  if (n <= 0) {
    dprintf("%s: invalid number(%d) of events\n",
      __func__, n);
    ret = -EINVAL;
    goto out;
  }

  ret = ioctl(fd, IHK_OS_AUX_PERF_GET, counter);
  if (ret) {
    ret = -errno;
    dprintf("%s: IHK_OS_AUX_PERF_GET returned %d\n",
      __func__, -ret);
    goto out;
  }

 out:
  if (fd != -1) {
    close(fd);
  }
  return ret;
}

int ihk_os_freeze(unsigned long *os_set, int n)
{
  int ret;
  int index;
  int fd = -1;

  dprintk("%s: enter\n", __func__);
  if (n <= 0) {
    dprintf("%s: invalid length of os bitset(%d)\n", __func__, n);
    ret = -EINVAL;
    goto out;
  }

  for (index = 0; index < n; index++) {
    if (*(os_set + index / 64) & (1ULL << (index % 64))) {
      if ((fd = ihklib_os_open(index)) < 0) {
        dprintf("%s: error: ihklib_os_open\n",
          __func__);
        ret = fd;
        goto out;
      }

      ret = ioctl(fd, IHK_OS_FREEZE, 0);
      if (ret) {
        ret = -errno;
        dprintf("%s: IHK_OS_FREEZE "
          "returned %d\n",
          __func__, -ret);
        goto out;
      }

      close(fd);
      fd = -1;
    }
  }
  ret = 0;
 out:
  if (fd != -1) {
    close(fd);
  }
  return ret;
}

int ihk_os_thaw(unsigned long *os_set, int n)
{
  int ret;
  int index;
  int fd = -1;

  dprintk("%s: enter\n", __func__);

  if (n <= 0) {
    dprintf("%s: invalid length of os bitset(%d)\n", __func__, n);
    ret = -EINVAL;
    goto out;
  }

  for (index = 0; index < n; index++) {
    if (*(os_set + index / 64) & (1ULL << (index % 64))) {
      if ((fd = ihklib_os_open(index)) < 0) {
        dprintf("%s: error: ihklib_os_open\n",
          __func__);
        ret = fd;
        goto out;
      }

      ret = ioctl(fd, IHK_OS_THAW, 0);
      if (ret) {
        ret = -errno;
        dprintf("%s: IHK_OS_THAW "
          "returned %d\n",
          __func__, -ret);
        goto out;
      }

      close(fd);
      fd = -1;
    }
  }
  ret = 0;
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
  int ret;
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
  char *token;

  dprintk("%s: enter\n", __func__);
  dprintf("%s: index=%d,dump_file=%s,dump_level=%d,interactive=%d\n",
    __func__, index, dump_file, dump_level, interactive);

  if ((osfd = ihklib_os_open(index)) < 0) {
    dprintf("%s: error: ihklib_os_open returned %d\n",
      __func__, osfd);
    ret = osfd;
    goto out;
  }

  ret = ihk_os_get_status(index);
  if (ret < 0) {
    dprintf("%s: ihk_os_get_status returned %d\n",
      __func__, ret);
    goto out;
  }

  if (ret == IHK_STATUS_INACTIVE) {
    ret = -EINVAL;
    goto out;
  }

  t = time(NULL);
  if (t == (time_t)-1) {
    ret = -errno;
    dprintf("%s: error: time returned %d\n",
      __func__, -ret);
    goto out;
  }

  tm = localtime(&t);
  if (!tm) {
    ret = -EINVAL;
    dprintf("%s: error: localtime failed\n",
      __func__);
    goto out;
  }

  error = gethostname(hname, sizeof(hname));
  if (error != 0) {
    ret = -errno;
    dprintf("%s: error: gethostname returned %d\n",
      __func__, -ret);
    goto out;
  }

  /* TODO: might be redundant */
  pw = getpwuid(getuid());
  if (pw == NULL) {
    ret = -errno;
    dprintf("%s: error: getpwuid returned %d\n",
      __func__, -ret);
    goto out;
  }

  args.cmd = DUMP_SET_LEVEL;
  args.level = dump_level;
  error = ioctl(osfd, IHK_OS_DUMP, &args);
  if (error != 0) {
    ret = -errno;
    dprintf("%s: error: DUMP_SET_LEVEL returned %d\n",
      __func__, -ret);
    goto out;
  }

  args.cmd = DUMP_NMI;
  error = ioctl(osfd, IHK_OS_DUMP, &args);
  if (error != 0) {
    ret = -errno;
    dprintf("%s: error: DUMP_NMI returned %d\n",
      __func__, -ret);
    goto out;
  }

  args.cmd = DUMP_QUERY_NUM_MEM_AREAS;
  args.size = 0;
  error = ioctl(osfd, IHK_OS_DUMP, &args);
  if (error != 0) {
    ret = -errno;
    dprintf("%s: error: "
      "DUMP_QUERY_NUM_MEM_AREAS returned %d\n",
      __func__, -ret);
    goto out;
  }

  mem_size = args.size;
  mem_chunks = malloc(mem_size);
  if (!mem_chunks) {
    ret = -ENOMEM;
    dprintf("%s: error: alloating mem_chunks\n",
      __func__);
    goto out;
  }

  memset(mem_chunks, 0, args.size);

  args.cmd = DUMP_QUERY_MEM_AREAS;
  args.buf = (void *)mem_chunks;
  error = ioctl(osfd, IHK_OS_DUMP, &args);
  if (error != 0) {
    ret = -errno;
    dprintf("%s: error: DUMP_QUERY_MEM_AREAS returned %d\n",
      __func__, -ret);
    goto out;
  }

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
  if (!buf) {
    ret = -ENOMEM;
    dprintf("%s: error: allocating buf\n", __func__);
    goto out;
  }

  bfd_init();

  if (dump_file == NULL) {
    ret = -EFAULT;
    goto out;
  }

  token = strrchr(dump_file, '/');
  if (token) {
    token[0] = 0;
    ret = access(dump_file, W_OK);
    if (ret) {
      ret = -errno;
      dprintf("%s: %s is inaccessible: %d\n",
        __func__, dump_file, -ret);
      token[0] = '/';
      goto out;
    }
    token[0] = '/';
  }

  abfd = bfd_fopen(dump_file, NULL, "w", -1);
  if (!abfd) {
    ret = -EINVAL;
    dprintf("%s: bfd_fopen failed: %s\n",
      __func__, bfd_errmsg(bfd_get_error()));
    goto out;
  }

  ok = bfd_set_format(abfd, bfd_object);
  if (!ok) {
    ret = -EINVAL;
    dprintf("%s: error: bfd_set_format: %s\n",
      __func__, bfd_errmsg(bfd_get_error()));
    goto out;
  }

  date = asctime(tm);
  if (date) {
    cpsize = strlen(date) - 1;  /* exclude trailing '\n' */
    scn = bfd_make_section_anyway(abfd, "date");
    if (!scn) {
      ret = -EINVAL;
      dprintf("%s: error: "
        "bfd_make_section_anyway(date): %s\n",
        __func__, bfd_errmsg(bfd_get_error()));
      goto out;
    }

    ok = bfd_set_section_size(abfd, scn, cpsize);
    if (!ok) {
      ret = -EINVAL;
      dprintf("%s: error: "
        "bfd_set_section_size: %s\n",
        __func__, bfd_errmsg(bfd_get_error()));
      goto out;
    }

    ok = bfd_set_section_flags(abfd, scn, SEC_HAS_CONTENTS);
    if (!ok) {
      ret = -EINVAL;
      dprintf("%s: error: "
        "bfd_set_section_flags: %s\n",
        __func__, bfd_errmsg(bfd_get_error()));
      goto out;
    }
  }
  error = gethostname(hname, sizeof(hname));
  if (!error) {
    cpsize = strlen(hname);
    scn = bfd_make_section_anyway(abfd, "hostname");
    if (!scn) {
      ret = -EINVAL;
      dprintf("%s: error: "
        "bfd_make_section_anyway(hostname): %s\n",
        __func__, bfd_errmsg(bfd_get_error()));
      goto out;
    }

    ok = bfd_set_section_size(abfd, scn, cpsize);
    if (!ok) {
      ret = -EINVAL;
      dprintf("%s: error: "
        "bfd_set_section_size: %s\n",
        __func__, bfd_errmsg(bfd_get_error()));
      goto out;
    }

    ok = bfd_set_section_flags(abfd, scn, SEC_HAS_CONTENTS);
    if (!ok) {
      ret = -EINVAL;
      dprintf("%s: error: "
        "bfd_set_section_flags: %s\n",
        __func__, bfd_errmsg(bfd_get_error()));
      goto out;
    }
  }
  pw = getpwuid(getuid());
  if (pw) {
    cpsize = strlen(pw->pw_name);
    scn = bfd_make_section_anyway(abfd, "user");
    if (!scn) {
      ret = -EINVAL;
      dprintf("%s: error: "
        "bfd_make_section_anyway(user): %s\n",
        __func__, bfd_errmsg(bfd_get_error()));
      goto out;
    }

    ok = bfd_set_section_size(abfd, scn, cpsize);
    if (!ok) {
      ret = -EINVAL;
      dprintf("%s: error: "
        "bfd_set_section_size: %s\n",
        __func__, bfd_errmsg(bfd_get_error()));
      goto out;
    }

    ok = bfd_set_section_flags(abfd, scn, SEC_HAS_CONTENTS);
    if (!ok) {
      ret = -EINVAL;
      dprintf("%s: error: "
        "bfd_set_section_flags: %s\n",
        __func__, bfd_errmsg(bfd_get_error()));
      goto out;
    }
  }

  /* Add section for physical memory chunks information */
  scn = bfd_make_section_anyway(abfd, "physchunks");
  if (!scn) {
    ret = -EINVAL;
    dprintf("%s: error: "
      "bfd_make_section_anyway(physchunks): %s\n",
      __func__, bfd_errmsg(bfd_get_error()));
    goto out;
  }

  ok = bfd_set_section_size(abfd, scn, mem_size);
  if (!ok) {
    ret = -EINVAL;
    dprintf("%s: error: "
      "bfd_set_section_size: %s\n",
      __func__, bfd_errmsg(bfd_get_error()));
    goto out;
  }

  ok = bfd_set_section_flags(abfd, scn, SEC_ALLOC|SEC_HAS_CONTENTS);
  if (!ok) {
    ret = -EINVAL;
    dprintf("%s: error: "
      "bfd_set_section_flags: %s\n",
      __func__, bfd_errmsg(bfd_get_error()));
    goto out;
  }

  for (i = 0; i < mem_chunks->nr_chunks; ++i) {

    physmem_name_buf = malloc(PHYSMEM_NAME_SIZE);
    memset(physmem_name_buf,0,PHYSMEM_NAME_SIZE);
    sprintf(physmem_name_buf, "physmem%d",i);

    /* Physical memory contents section */
    scn = bfd_make_section_anyway(abfd, physmem_name_buf);
    if (!scn) {
      ret = -EINVAL;
      dprintf("%s: error: "
        "bfd_make_section_anyway(physmem): %s\n",
        __func__, bfd_errmsg(bfd_get_error()));
      goto out;
    }

    if (interactive) {
      ok = bfd_set_section_size(abfd, scn, PAGE_SIZE);
    }
    else {
      ok = bfd_set_section_size(abfd, scn, mem_chunks->chunks[i].size);
    }
    if (!ok) {
      ret = -EINVAL;
      dprintf("%s: error: "
        "bfd_set_section_size: %s\n",
        __func__, bfd_errmsg(bfd_get_error()));
      goto out;
    }

    ok = bfd_set_section_flags(abfd, scn, SEC_ALLOC|SEC_HAS_CONTENTS);
    if (!ok) {
      ret = -EINVAL;
      dprintf("%s: error: "
        "bfd_set_section_flags: %s\n",
        __func__, bfd_errmsg(bfd_get_error()));
      goto out;
    }

    scn->vma = mem_chunks->chunks[i].addr;
  }

  scn = bfd_get_section_by_name(abfd, "date");
  if (scn) {
    ok = bfd_set_section_contents(abfd, scn, date, 0, scn->size);
    if (!ok) {
      ret = -EINVAL;
      dprintf("%s: error: "
        "bfd_set_section_contents(date): %s\n",
        __func__, bfd_errmsg(bfd_get_error()));
      goto out;
    }
  }

  scn = bfd_get_section_by_name(abfd, "hostname");
  if (scn) {
    ok = bfd_set_section_contents(abfd, scn, hname, 0, scn->size);
    if (!ok) {
      ret = -EINVAL;
      dprintf("%s: error: "
        "bfd_set_section_contents(hostname): %s\n",
        __func__, bfd_errmsg(bfd_get_error()));
      goto out;
    }
  }

  scn = bfd_get_section_by_name(abfd, "user");
  if (scn) {
    ok = bfd_set_section_contents(abfd, scn, pw->pw_name, 0, scn->size);
    if (!ok) {
      ret = -EINVAL;
      dprintf("%s: error: "
        "bfd_set_section_contents(user): %s\n",
        __func__, bfd_errmsg(bfd_get_error()));
      goto out;
    }
  }

  scn = bfd_get_section_by_name(abfd, "physchunks");
  if (scn) {
    ok = bfd_set_section_contents(abfd, scn, mem_chunks, 0, mem_size);
    if (!ok) {
      ret = -EINVAL;
      dprintf("%s: error: "
        "bfd_set_section_contents(physchunks): %s\n",
        __func__, bfd_errmsg(bfd_get_error()));
      goto out;
    }
  }

  if (interactive) {
    ret = 0;
    goto out;
  }

  for (i = 0; i < mem_chunks->nr_chunks; ++i) {

    phys_offset = 0;

    memset(physmem_name,0,sizeof(physmem_name));
    sprintf(physmem_name, "physmem%d",i);

    scn = bfd_get_section_by_name(abfd, physmem_name);
    if (!scn) {
      ret = -EINVAL;
      dprintf("%s: error: "
        "bfd_get_section_by_name(physmem_name): %s\n",
        __func__, bfd_errmsg(bfd_get_error()));
      goto out;
    }

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
      if (error != 0) {
        ret = -errno;
        dprintf("%s: error: DUMP_HEAD returned %d\n",
          __func__, -ret);
        goto out;
      }

      ok = bfd_set_section_contents(abfd, scn, buf, phys_offset, cpsize);
      if (!ok) {
        ret = -EINVAL;
        dprintf("%s: error: "
          "bfd_set_section_contents(physmem): %s\n",
          __func__, bfd_errmsg(bfd_get_error()));
        goto out;
      }

      phys_offset += cpsize;
    }
  }

  ret = 0;
 out:
  if (abfd) {
    ok = bfd_close(abfd);
    if (!ok) {
      ret = -EINVAL;
      dprintf("%s: error: bfd_close: %s\n",
        __func__, bfd_errmsg(bfd_get_error()));
    }
  }
  if (osfd >= 0) {
    error = close(osfd);
    if (error) {
      ret = -errno;
      dprintf("%s: error: close: %s\n",
        __func__, strerror(-ret));
    }
  }
  return ret;
}
#else /* ENABLE_MEMDUMP */
int ihk_os_makedumpfile(int index, char *dump_file, int dump_level, int interactive)
{
  dprintk("%s: enter\n", __func__);
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
  dprintk("%s: enter\n", __func__);
  loglevel = level;
  return 0;
}
