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
    return data->ops->assign_cpu(data, data->priv, arg);

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
    return data->ops->release_cpu(data, data->priv, arg);

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
    return data->ops->set_ikc_map(data, data->priv, arg);

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
    return data->ops->get_ikc_map(data, data->priv, arg);

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
    return data->ops->query_cpu(data, data->priv,arg);

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
    return data->ops->get_num_cpus(data, data->priv);

  unsigned long ivec = 0;
  unsigned long total_branch = 2;
  int ret;

  branch_info_t b_infos[] = {
    { -1, "invalid handler" },
    { 0,  "main case" },
  };

  for (ivec = 0; ivec < total_branch; ++ivec) {
    START(b_infos[ivec].name);

    if (ivec == 0 || (!data->ops || !data->ops->get_num_cpus)) {
      ret = -1;
      if (ivec == 0) goto out;
      goto err;
    }

    ret = data->ops->get_num_cpus(data, data->priv);

   out:
    BRANCH_RET_CHK(ret, b_infos[ivec].expected);
  }

  return ret;
 err:
  return -1;
}

IHK_OS_OPS_BEGIN(int, assign_mem,
                 unsigned long arg)
{
  IHK_OPS_BODY(assign_mem, arg);
}

IHK_OS_OPS_BEGIN(int, release_mem,
                 unsigned long arg)
{
  IHK_OPS_BODY(release_mem, arg);
}

IHK_OS_OPS_BEGIN(int, query_mem,
                 unsigned long arg)
{
  IHK_OPS_BODY(query_mem, arg);
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

IHK_OS_OPS_BEGIN(int, register_handler, int itype,
                 struct ihk_host_interrupt_handler *h)
{
  IHK_OPS_BODY(register_handler, itype, h);
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

IHK_OS_OPS_BEGIN(int, get_special_addr, enum ihk_special_addr_type type,
                 unsigned long *address, unsigned long *size)
{
  IHK_OPS_BODY(get_special_addr, type, address, size);
}

IHK_OS_OPS_BEGIN(int, wait_for_status, enum ihk_os_status status,
                 int sleepable, int timeout)
{
  IHK_OPS_BODY(wait_for_status, status, sleepable, timeout);
}

IHK_OS_OPS_BEGIN(int, issue_interrupt, int cpu, int vector)
{
  IHK_OPS_BODY(issue_interrupt, cpu, vector);
}

IHK_OS_OPS_BEGIN(int, send_nmi_orig, int mode)
{
  IHK_OPS_BODY(send_nmi, mode);
}

IHK_OS_OPS_BEGIN(int, send_nmi, int mode)
{
  if (g_ihk_test_mode != TEST__IHK_OS_SEND_NMI)  // Disable test code
    return data->ops->send_nmi(data, data->priv, mode);

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

IHK_OS_OPS_BEGIN_NOARG(struct ihk_mem_info *, get_memory_info)
{
  IHK_OPS_BODY_PTR_NOARG(get_memory_info);
}

IHK_OS_OPS_BEGIN_NOARG(struct ihk_cpu_info *, get_cpu_info)
{
  IHK_OPS_BODY_PTR_NOARG(get_cpu_info);
}

IHK_OS_OPS_BEGIN_NOARG(int, query_status)
{
  IHK_OPS_BODY_NOARG(query_status);
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


IHK_DEV_OPS_BEGIN(unsigned long, map_memory,
                 unsigned long rphys, unsigned long size)
{
  IHK_OPS_BODY(map_memory, rphys, size);
}

IHK_DEV_OPS_BEGIN(int, unmap_memory, unsigned long lphys, unsigned long size)
{
  IHK_OPS_BODY(unmap_memory, lphys, size);
}

IHK_DEV_OPS_BEGIN(void *, map_virtual, unsigned long phys, unsigned long size,
                  void *virtual, int flags)
{
  IHK_OPS_BODY_PTR(map_virtual, phys, size, virtual, flags);
}

IHK_DEV_OPS_BEGIN(int, unmap_virtual_orig, void *virtual, int flags)
{
  IHK_OPS_BODY(unmap_virtual, virtual, flags);
}

IHK_DEV_OPS_BEGIN(int, unmap_virtual, void *virtual, int flags)
{
  if (g_ihk_test_mode != TEST__IHK_DEVICE_UNMAP_VIRTUAL)  // Disable test code
    return data->ops->unmap_virtual(data, data->priv, virtual, flags);

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
