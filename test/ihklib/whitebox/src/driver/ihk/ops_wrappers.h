/**
 * \file ops_wrappers.h
 *
 * \brief IHK-Host:
 * Macros to generate static functions that invoke "*_ops" functions.
 * All functions are prefixed with "__ihk_device_" or "__ihk_os_".
 *
 * \author Taku Shimosawa  <shimosawa@is.s.u-tokyo.ac.jp> \par
 * Copyright (C) 2011 - 2012  Taku Shimosawa
 *
 * \author Balazs Gerofi  <bgerofi@riken.jp> \par
 * Copyright (C) 2012  RIKEN AICS
 *
 * HISTORY:
 *  2013/06/24: bgerofi - interface for querying free physical memory on OS
 *
 */
#ifndef __HEADER_IHK_OPS_WRAPPERS_H
#define __HEADER_IHK_OPS_WRAPPERS_H

#include "host_linux.h"
#include "driver/okng_driver.h"
#include "branch_info.h"

#define IHK_DEV_OPS_BEGIN(rettype, name, ...) \
  static rettype __ihk_device_##name(struct ihk_host_linux_device_data *\
                                     data, \
                                     __VA_ARGS__)

#define IHK_OS_OPS_BEGIN(rettype, name, ...) \
  static rettype __ihk_os_##name(struct ihk_host_linux_os_data *data, \
                                 __VA_ARGS__)

#define IHK_OS_OPS_BEGIN_NOARG(rettype, name) \
  static rettype __ihk_os_##name(struct ihk_host_linux_os_data *data)

#define IHK_OPS_BODY(name, ...)    \
  do { \
    if (data->ops->name) { \
      return data->ops->name(data, data->priv, __VA_ARGS__); \
    } else { \
      return -EINVAL; \
    } \
  } while (0)

#define IHK_OPS_BODY_PTR(name, ...)    \
  do { \
    if (data->ops->name) { \
      return data->ops->name(data, data->priv, __VA_ARGS__); \
    } else { \
      return NULL; \
    } \
  } while (0)

#define IHK_OPS_BODY_NOARG(name)    \
  do { \
    if (data->ops->name) { \
      return data->ops->name(data, data->priv); \
    } else { \
      return -EINVAL; \
    } \
  } while (0)

#define IHK_OPS_BODY_PTR_NOARG(name)    \
  do { \
    if (data->ops->name) { \
      return data->ops->name(data, data->priv); \
    } else { \
      return NULL; \
    } \
  } while (0)

#define IHK_OPS_BODY_VOID(name, ...)    \
  do { \
    if (data->ops->name) { \
      return data->ops->name(data, data->priv, __VA_ARGS__); \
    } \
  } while (0)

#define IHK_OPS_BODY_VOID_NOARG(name)    \
  do { \
    if (data->ops->name) { \
      data->ops->name(data, data->priv); \
    } \
  } while (0)

IHK_OS_OPS_BEGIN(int, assign_cpu_orig,
                 unsigned long arg)
{
  IHK_OPS_BODY(assign_cpu, arg);
}

IHK_OS_OPS_BEGIN(int, assign_cpu,
                 unsigned long arg)
{
  if (g_ihk_test_mode != TEST__IHK_OS_ASSIGN_CPU)  // Disable test code
    return __ihk_os_assign_cpu_orig(data, arg);

  unsigned long ivec = 0;
  unsigned long total_branch = 2;
  int ret;

  branch_info_t b_infos[] = {
    { -1, "invalid handler" },
    { 0,  "main case" },
  };

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    if (ivec == 0 || (!data->ops || !data->ops->assign_cpu)) {
      ret = -1;
      if (ivec == 0) goto out;
      goto err;
    }

    ret = data->ops->assign_cpu(data, data->priv, arg);

   out:
    BRANCH_RET_CHK(ret, b_infos[ivec].expected);
  }

  return ret;
 err:
  return -1;
}

IHK_OS_OPS_BEGIN(int, release_cpu_orig,
                 unsigned long arg)
{
  IHK_OPS_BODY(release_cpu, arg);
}

IHK_OS_OPS_BEGIN(int, release_cpu,
                 unsigned long arg)
{
  if (g_ihk_test_mode != TEST__IHK_OS_RELEASE_CPU)  // Disable test code
    return __ihk_os_release_cpu_orig(data, arg);

  unsigned long ivec = 0;
  unsigned long total_branch = 2;
  int ret;

  branch_info_t b_infos[] = {
    { -1, "invalid handler" },
    { 0,  "main case" },
  };

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    if (ivec == 0 || (!data->ops || !data->ops->release_cpu)) {
      ret = -1;
      if (ivec == 0) goto out;
      goto err;
    }

    ret = data->ops->release_cpu(data, data->priv, arg);

   out:
    BRANCH_RET_CHK(ret, b_infos[ivec].expected);
  }

  return ret;
 err:
  return -1;
}

IHK_OS_OPS_BEGIN(int, set_ikc_map_orig,
                 unsigned long arg)
{
  IHK_OPS_BODY(set_ikc_map, arg);
}

IHK_OS_OPS_BEGIN(int, set_ikc_map,
                 unsigned long arg)
{
  if (g_ihk_test_mode != TEST__IHK_OS_SET_IKC_MAP)  // Disable test code
    return __ihk_os_set_ikc_map_orig(data, arg);

  unsigned long ivec = 0;
  unsigned long total_branch = 2;
  int ret;

  branch_info_t b_infos[] = {
    { -1, "invalid handler" },
    { 0,  "main case" },
  };

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    if (ivec == 0 || (!data->ops || !data->ops->set_ikc_map)) {
      ret = -1;
      if (ivec == 0) goto out;
      goto err;
    }

    ret = data->ops->set_ikc_map(data, data->priv, arg);

   out:
    BRANCH_RET_CHK(ret, b_infos[ivec].expected);
  }

  return ret;
 err:
  return -1;
}

IHK_OS_OPS_BEGIN(int, get_ikc_map_orig,
                 unsigned long arg)
{
  IHK_OPS_BODY(get_ikc_map, arg);
}

IHK_OS_OPS_BEGIN(int, get_ikc_map,
                 unsigned long arg)
{
  if (g_ihk_test_mode != TEST__IHK_OS_GET_IKC_MAP)  // Disable test code
    return __ihk_os_get_ikc_map_orig(data, arg);

  unsigned long ivec = 0;
  unsigned long total_branch = 2;
  int ret;

  branch_info_t b_infos[] = {
    { -1, "invalid handler" },
    { 0,  "main case" },
  };

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    if (ivec == 0 || (!data->ops || !data->ops->get_ikc_map)) {
      ret = -1;
      if (ivec == 0) goto out;
      goto err;
    }

    ret = data->ops->get_ikc_map(data, data->priv, arg);

   out:
    BRANCH_RET_CHK(ret, b_infos[ivec].expected);
  }

  return ret;
 err:
  return -1;
}

IHK_OS_OPS_BEGIN(int, get_buildid,
                 unsigned long arg)
{
  IHK_OPS_BODY(get_buildid, arg);
}

IHK_OS_OPS_BEGIN(int, query_cpu_orig,
                 unsigned long arg)
{
  IHK_OPS_BODY(query_cpu, arg);
}

IHK_OS_OPS_BEGIN(int, query_cpu,
                 unsigned long arg)
{
  if (g_ihk_test_mode != TEST__IHK_OS_QUERY_CPU)  // Disable test code
    return __ihk_os_query_cpu_orig(data, arg);

  unsigned long ivec = 0;
  unsigned long total_branch = 2;
  int ret;

  branch_info_t b_infos[] = {
    { -1, "invalid handler" },
    { 0,  "main case" },
  };

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    if (ivec == 0 || (!data->ops || !data->ops->query_cpu)) {
      ret = -1;
      if (ivec == 0) goto out;
      goto err;
    }

    ret = data->ops->query_cpu(data, data->priv, arg);

   out:
    BRANCH_RET_CHK(ret, b_infos[ivec].expected);
  }

  return ret;
 err:
  return -1;
}

IHK_OS_OPS_BEGIN_NOARG(int, get_num_cpus_orig)
{
  IHK_OPS_BODY_NOARG(get_num_cpus);
}

IHK_OS_OPS_BEGIN_NOARG(int, get_num_cpus)
{
  if (g_ihk_test_mode != TEST__IHK_OS_GET_NUM_CPUS)  // Disable test code
    return __ihk_os_get_num_cpus_orig(data);

  unsigned long ivec = 0;
  unsigned long total_branch = 2;
  int ret;
  int num_cpus_ = -1;

  branch_info_t b_infos[] = {
    { -1, "invalid handler" },
    { 0,  "main case" },
  };

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);
    num_cpus_ = -1;

    if (ivec == 0 || (!data->ops || !data->ops->get_num_cpus)) {
      ret = -1;
      if (ivec == 0) goto out;
      goto err;
    }

    num_cpus_ = data->ops->get_num_cpus(data, data->priv);
    if (num_cpus_ >= 0) ret = 0;
   out:
    BRANCH_RET_CHK(ret, b_infos[ivec].expected);
    ret = num_cpus_;
  }

  return ret;
 err:
  return -1;
}

IHK_OS_OPS_BEGIN(int, assign_mem_orig,
                 unsigned long arg)
{
  IHK_OPS_BODY(assign_mem, arg);
}

IHK_OS_OPS_BEGIN(int, assign_mem,
                 unsigned long arg)
{
  if (g_ihk_test_mode != TEST__IHK_OS_ASSIGN_MEM)  // Disable test code
    return __ihk_os_assign_mem_orig(data, arg);

  unsigned long ivec = 0;
  unsigned long total_branch = 2;
  int ret;

  branch_info_t b_infos[] = {
    { -1, "invalid handler" },
    { 0,  "main case" },
  };

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    if (ivec == 0 || (!data->ops || !data->ops->assign_mem)) {
      ret = -1;
      if (ivec == 0) goto out;
      goto err;
    }

    ret = data->ops->assign_mem(data, data->priv, arg);

   out:
    BRANCH_RET_CHK(ret, b_infos[ivec].expected);
  }

  return ret;
 err:
  return -1;
}

IHK_OS_OPS_BEGIN(int, release_mem_orig,
                 unsigned long arg)
{
  IHK_OPS_BODY(release_mem, arg);
}

IHK_OS_OPS_BEGIN(int, release_mem,
                 unsigned long arg)
{
  if (g_ihk_test_mode != TEST__IHK_OS_RELEASE_MEM)  // Disable test code
    return __ihk_os_release_mem_orig(data, arg);

  unsigned long ivec = 0;
  unsigned long total_branch = 2;
  int ret;

  branch_info_t b_infos[] = {
    { -1, "invalid handler" },
    { 0,  "main case" },
  };

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    if (ivec == 0 || (!data->ops || !data->ops->release_mem)) {
      ret = -1;
      if (ivec == 0) goto out;
      goto err;
    }

    ret = data->ops->release_mem(data, data->priv, arg);

   out:
    BRANCH_RET_CHK(ret, b_infos[ivec].expected);
  }

  return ret;
 err:
  return -1;
}

IHK_OS_OPS_BEGIN(int, query_mem_orig,
                 unsigned long arg)
{
  IHK_OPS_BODY(query_mem, arg);
}

IHK_OS_OPS_BEGIN(int, query_mem,
                 unsigned long arg)
{
  if (g_ihk_test_mode != TEST__IHK_OS_QUERY_MEM)  // Disable test code
    return __ihk_os_query_mem_orig(data, arg);

  unsigned long ivec = 0;
  unsigned long total_branch = 2;
  int ret;

  branch_info_t b_infos[] = {
    { -1, "invalid handler" },
    { 0,  "main case" },
  };

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    if (ivec == 0 || (!data->ops || !data->ops->query_mem)) {
      ret = -1;
      if (ivec == 0) goto out;
      goto err;
    }

    ret = data->ops->query_mem(data, data->priv, arg);

   out:
    BRANCH_RET_CHK(ret, b_infos[ivec].expected);
  }

  return ret;
 err:
  return -1;
}

IHK_OS_OPS_BEGIN(unsigned long, map_memory,
                 unsigned long rphys, unsigned long size)
{
  IHK_OPS_BODY(map_memory, rphys, size);
}

IHK_OS_OPS_BEGIN(int, unmap_memory, unsigned long lphys, unsigned long size)
{
  IHK_OPS_BODY(unmap_memory, lphys, size);
}

IHK_OS_OPS_BEGIN(int, register_handler_orig, int itype,
                 struct ihk_host_interrupt_handler *h)
{
  IHK_OPS_BODY(register_handler, itype, h);
}

IHK_OS_OPS_BEGIN(int, register_handler, int itype,
                 struct ihk_host_interrupt_handler *h)
{
  if (g_ihk_test_mode != TEST__IHK_OS_REGISTER_HANDLER)  // Disable test code
    return __ihk_os_register_handler_orig(data, itype, h);

  unsigned long ivec = 0;
  unsigned long total_branch = 2;
  int ret;

  branch_info_t b_infos[] = {
    { -1, "invalid handler" },
    { 0,  "main case" },
  };

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    if (ivec == 0 || (!data->ops || !data->ops->register_handler)) {
      ret = -1;
      if (ivec == 0) goto out;
      goto err;
    }

    ret = data->ops->register_handler(data, data->priv, itype, h);

   out:
    BRANCH_RET_CHK(ret, b_infos[ivec].expected);
  }

  return ret;
 err:
  return -1;
}

IHK_OS_OPS_BEGIN(int, unregister_handler, int itype,
                 struct ihk_host_interrupt_handler *h)
{
  IHK_OPS_BODY(unregister_handler, itype, h);
}

IHK_OS_OPS_BEGIN(int, alloc_resource, struct ihk_resource *resource)
{
  IHK_OPS_BODY(alloc_resource, resource);
}

IHK_OS_OPS_BEGIN(int, get_special_addr_orig, enum ihk_special_addr_type type,
                 unsigned long *address, unsigned long *size)
{
  IHK_OPS_BODY(get_special_addr, type, address, size);
}

IHK_OS_OPS_BEGIN(int, get_special_addr, enum ihk_special_addr_type type,
                 unsigned long *address, unsigned long *size)
{
  if (g_ihk_test_mode != TEST__IHK_OS_GET_SPECIAL_ADDR)  // Disable test code
    return __ihk_os_get_special_addr_orig(data, type, address, size);

  unsigned long ivec = 0;
  unsigned long total_branch = 2;
  int ret;

  branch_info_t b_infos[] = {
    { -1, "invalid handler" },
    { 0,  "main case" },
  };

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    if (ivec == 0 || (!data->ops || !data->ops->get_special_addr)) {
      ret = -1;
      if (ivec == 0) goto out;
      goto err;
    }

    ret = data->ops->get_special_addr(data, data->priv, type, address, size);

   out:
    BRANCH_RET_CHK(ret, b_infos[ivec].expected);
  }

  return ret;
 err:
  return -1;
}

IHK_OS_OPS_BEGIN(int, wait_for_status, enum ihk_os_status status,
                 int sleepable, int timeout)
{
  IHK_OPS_BODY(wait_for_status, status, sleepable, timeout);
}

IHK_OS_OPS_BEGIN(int, issue_interrupt_orig, int cpu, int vector)
{
  IHK_OPS_BODY(issue_interrupt, cpu, vector);
}

IHK_OS_OPS_BEGIN(int, issue_interrupt, int cpu, int vector)
{
  if (g_ihk_test_mode != TEST__IHK_OS_ISSUE_INTERRUPT)  // Disable test code
    return __ihk_os_issue_interrupt_orig(data, cpu, vector);

  unsigned long ivec = 0;
  unsigned long total_branch = 2;
  int ret;

  branch_info_t b_infos[] = {
    { -1,      "invalid handler" },
    { -EINVAL, "main case" },
  };

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    if (ivec == 0 || (!data->ops || !data->ops->issue_interrupt)) {
      ret = -1;
      if (ivec == 0) goto out;
      goto err;
    }

    ret = data->ops->issue_interrupt(data, data->priv, cpu, vector);

   out:
    BRANCH_RET_CHK(ret, b_infos[ivec].expected);
  }

  return ret;
 err:
  return -1;
}

IHK_OS_OPS_BEGIN(int, send_nmi_orig, int mode)
{
  IHK_OPS_BODY(send_nmi, mode);
}

IHK_OS_OPS_BEGIN(int, send_nmi, int mode)
{
  if (g_ihk_test_mode != TEST__IHK_OS_SEND_NMI)  // Disable test code
    return __ihk_os_send_nmi_orig(data, mode);

  unsigned long ivec = 0;
  unsigned long total_branch = 2;
  int ret;

  branch_info_t b_infos[] = {
    { -1, "invalid handler" },
    { 0,  "main case" },
  };

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    if (ivec == 0 || (!data->ops || !data->ops->send_nmi)) {
      ret = -1;
      if (ivec == 0) goto out;
      goto err;
    }

    ret = data->ops->send_nmi(data, data->priv, mode);

   out:
    BRANCH_RET_CHK(ret, b_infos[ivec].expected);
  }

  return ret;
 err:
  return -1;
}

IHK_OS_OPS_BEGIN_NOARG(struct ihk_mem_info *, get_memory_info_orig)
{
  IHK_OPS_BODY_PTR_NOARG(get_memory_info);
}

IHK_OS_OPS_BEGIN_NOARG(struct ihk_mem_info *, get_memory_info)
{
  if (g_ihk_test_mode != TEST__IHK_OS_GET_MEMORY_INFO)  // Disable test code
    return __ihk_os_get_memory_info_orig(data);

  unsigned long ivec = 0;
  unsigned long total_branch = 2;
  struct ihk_mem_info *ret;

  branch_info_t b_infos[] = {
    { 0, "invalid handler" },
    { 0, "main case" },
  };

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    if (ivec == 0 || (!data->ops || !data->ops->get_memory_info)) {
      ret = NULL;
      if (ivec == 0) goto out;
      goto err;
    }

    ret = data->ops->get_memory_info(data, data->priv);

   out:
    if (ivec == total_branch - 1) {
      OKNG(ret, "should return a valid memory info\n");
    } else {
      OKNG(!ret, "cannot get memory info\n");
    }
  }

  return ret;
 err:
  return NULL;
}

IHK_OS_OPS_BEGIN_NOARG(struct ihk_cpu_info *, get_cpu_info_orig)
{
  IHK_OPS_BODY_PTR_NOARG(get_cpu_info);
}

IHK_OS_OPS_BEGIN_NOARG(struct ihk_cpu_info *, get_cpu_info)
{
  if (g_ihk_test_mode != TEST__IHK_OS_GET_CPU_INFO)  // Disable test code
    return __ihk_os_get_cpu_info_orig(data);

  unsigned long ivec = 0;
  unsigned long total_branch = 2;
  struct ihk_cpu_info *ret;

  branch_info_t b_infos[] = {
    { 0, "invalid handler" },
    { 0, "main case" },
  };

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    if (ivec == 0 || (!data->ops || !data->ops->get_cpu_info)) {
      ret = NULL;
      if (ivec == 0) goto out;
      goto err;
    }

    ret = data->ops->get_cpu_info(data, data->priv);

   out:
    if (ivec == total_branch - 1) {
      OKNG(ret, "should return a valid cpu info\n");
    } else {
      OKNG(!ret, "cannot get cpu info\n");
    }
  }

  return ret;
 err:
  return NULL;
}

IHK_OS_OPS_BEGIN_NOARG(int, query_status_orig)
{
  IHK_OPS_BODY_NOARG(query_status);
}

IHK_OS_OPS_BEGIN_NOARG(int, query_status)
{
  if (g_ihk_test_mode != TEST__IHK_OS_QUERY_STATUS)  // Disable test code
    return __ihk_os_query_status_orig(data);

  unsigned long ivec = 0;
  unsigned long total_branch = 2;
  int ret;

  branch_info_t b_infos[] = {
    { -1, "invalid handler" },
    { 0,  "main case" },
  };

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);
    int output = -1;
    if (ivec == 0 || (!data->ops || !data->ops->query_status)) {
      ret = -1;
      if (ivec == 0) goto out;
      goto err;
    }

    output = data->ops->query_status(data, data->priv);
    ret = 0;
   out:
    BRANCH_RET_CHK(ret, b_infos[ivec].expected);
    ret = output;
  }

  return ret;
 err:
  return -1;
}

IHK_OS_OPS_BEGIN_NOARG(void, notify_hungup)
{
  IHK_OPS_BODY_VOID_NOARG(notify_hungup);
}

IHK_OS_OPS_BEGIN_NOARG(int, get_num_numa_nodes)
{
  IHK_OPS_BODY_NOARG(get_num_numa_nodes);
}

IHK_OS_OPS_BEGIN_NOARG(int, query_free_mem)
{
  IHK_OPS_BODY_NOARG(query_free_mem);
}


IHK_DEV_OPS_BEGIN(unsigned long, map_memory_orig,
                 unsigned long rphys, unsigned long size)
{
  IHK_OPS_BODY(map_memory, rphys, size);
}

IHK_DEV_OPS_BEGIN(unsigned long, map_memory,
                 unsigned long rphys, unsigned long size)
{
  if (g_ihk_test_mode != TEST__IHK_DEVICE_MAP_MEMORY)  // Disable test code
    return __ihk_device_map_memory_orig(data, rphys, size);

  unsigned long ivec = 0;
  unsigned long total_branch = 2;
  unsigned long ret;

  branch_info_t b_infos[] = {
    { 0, "invalid handler" },
    { 0, "main case" },
  };

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    if (ivec == 0 || (!data->ops || !data->ops->map_memory)) {
      ret = 0;
      if (ivec == 0) goto out;
      goto err;
    }

    ret = data->ops->map_memory(data, data->priv, rphys, size);

   out:
    if (ivec == total_branch - 1) {
      OKNG(ret, "should return a valid address\n");
    } else {
      OKNG(!ret, "cannot map memory\n");
    }
  }

  return ret;
 err:
  return 0;
}

IHK_DEV_OPS_BEGIN(int, unmap_memory, unsigned long lphys, unsigned long size)
{
  IHK_OPS_BODY(unmap_memory, lphys, size);
}

IHK_DEV_OPS_BEGIN(void *, map_virtual_orig, unsigned long phys,
                  unsigned long size, void *virtual, int flags)
{
  IHK_OPS_BODY_PTR(map_virtual, phys, size, virtual, flags);
}

IHK_DEV_OPS_BEGIN(void *, map_virtual, unsigned long phys,
                  unsigned long size, void *virtual, int flags)
{
  if (g_ihk_test_mode != TEST__IHK_DEVICE_MAP_VIRTUAL)  // Disable test code
    return __ihk_device_map_virtual_orig(data, phys, size, virtual, flags);

  unsigned long ivec = 0;
  unsigned long total_branch = 2;
  void *ret;

  branch_info_t b_infos[] = {
    { 0, "invalid handler" },
    { 0, "main case" },
  };

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    if (ivec == 0 || (!data->ops || !data->ops->map_virtual)) {
      ret = NULL;
      if (ivec == 0) goto out;
      goto err;
    }

    ret = data->ops->map_virtual(data, data->priv, phys, size, virtual, flags);

   out:
    if (ivec == total_branch - 1) {
      OKNG(ret, "should return a valid address\n");
    } else {
      OKNG(!ret, "cannot map virtual address\n");
    }
  }

  return ret;
 err:
  return NULL;
}

IHK_DEV_OPS_BEGIN(int, unmap_virtual_orig, void *virtual, int flags)
{
  IHK_OPS_BODY(unmap_virtual, virtual, flags);
}

IHK_DEV_OPS_BEGIN(int, unmap_virtual, void *virtual, int flags)
{
  if (g_ihk_test_mode != TEST__IHK_DEVICE_UNMAP_VIRTUAL)  // Disable test code
    return __ihk_device_unmap_virtual_orig(data, virtual, flags);

  unsigned long ivec = 0;
  unsigned long total_branch = 2;
  int ret;

  branch_info_t b_infos[] = {
    { -1, "invalid handler" },
    { 0,  "main case" },
  };

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    if (ivec == 0 || (!data->ops || !data->ops->unmap_virtual)) {
      ret = -1;
      if (ivec == 0) goto out;
      goto err;
    }

    ret = data->ops->unmap_virtual(data, data->priv, virtual, flags);

   out:
    BRANCH_RET_CHK(ret, b_infos[ivec].expected);
  }

  return ret;
 err:
  return -1;
}

IHK_DEV_OPS_BEGIN(ihk_dma_channel_t, get_dma_channel, int channel)
{
  IHK_OPS_BODY_PTR(get_dma_channel, channel);
}
IHK_DEV_OPS_BEGIN(int, get_dma_info, struct ihk_dma_info *info)
{
  IHK_OPS_BODY(get_dma_info, info);
}

IHK_DEV_OPS_BEGIN(struct ihk_cpu_topology *, get_cpu_topology, int hw_id)
{
  IHK_OPS_BODY_PTR(get_cpu_topology, hw_id);
}

IHK_DEV_OPS_BEGIN(struct ihk_node_topology *, get_node_topology, int node)
{
  IHK_OPS_BODY_VOID(get_node_topology, node);
  return ERR_PTR(-EINVAL);
}

IHK_DEV_OPS_BEGIN(int, linux_cpu_to_hw_id, int cpu)
{
  IHK_OPS_BODY(linux_cpu_to_hw_id, cpu);
}

#endif
