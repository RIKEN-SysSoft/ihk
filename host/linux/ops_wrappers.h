#ifndef __HEADER_AAL_OPS_WRAPPERS_H
#define __HEADER_AAL_OPS_WRAPPERS_H

#include "host_linux.h"

#define AAL_DEV_OPS_BEGIN(rettype, name, ...) \
	static rettype __aal_device_##name(struct aal_host_linux_device_data *\
	                                   data, \
	                                   __VA_ARGS__)

#define AAL_OS_OPS_BEGIN(rettype, name, ...) \
	static rettype __aal_os_##name(struct aal_host_linux_os_data *data, \
	                               __VA_ARGS__)

#define AAL_OS_OPS_BEGIN_NOARG(rettype, name) \
	static rettype __aal_os_##name(struct aal_host_linux_os_data *data)

#define AAL_OPS_BODY(name, ...)	  \
	do { \
	if (data->ops->name) { \
		return data->ops->name(data, data->priv, __VA_ARGS__); \
	} else { \
		return -EINVAL; \
	} \
	} while (0)

#define AAL_OPS_BODY_PTR(name, ...)	  \
	do { \
	if (data->ops->name) { \
		return data->ops->name(data, data->priv, __VA_ARGS__); \
	} else { \
		return NULL; \
	} \
	} while (0)

#define AAL_OPS_BODY_PTR_NOARG(name)	  \
	do { \
	if (data->ops->name) { \
		return data->ops->name(data, data->priv); \
	} else { \
		return NULL; \
	} \
	} while (0)

#define AAL_OPS_BODY_VOID(name, ...)	  \
	do { \
	if (data->ops->name) { \
		return data->ops->name(data, data->priv, __VA_ARGS__); \
	} while (0)

AAL_OS_OPS_BEGIN(unsigned long, map_memory,
                 unsigned long rphys, unsigned long size)
{
	AAL_OPS_BODY(map_memory, rphys, size);
}

AAL_OS_OPS_BEGIN(int, unmap_memory, unsigned long lphys, unsigned long size)
{
	AAL_OPS_BODY(unmap_memory, lphys, size);
}

AAL_OS_OPS_BEGIN(int, register_handler, int itype,
                 struct aal_host_interrupt_handler *h)
{
	AAL_OPS_BODY(register_handler, itype, h);
}

AAL_OS_OPS_BEGIN(int, unregister_handler, int itype,
                 struct aal_host_interrupt_handler *h)
{
	AAL_OPS_BODY(unregister_handler, itype, h);
}

AAL_OS_OPS_BEGIN(int, get_special_addr, enum aal_special_addr_type type,
                 unsigned long *address, unsigned long *size)
{
	AAL_OPS_BODY(get_special_addr, type, address, size);
}

AAL_OS_OPS_BEGIN(int, wait_for_status, enum aal_os_status status,
                 int sleepable, int timeout)
{
	AAL_OPS_BODY(wait_for_status, status, sleepable, timeout);
}

AAL_OS_OPS_BEGIN(int, issue_interrupt, int cpu, int vector)
{
	AAL_OPS_BODY(issue_interrupt, cpu, vector);
}

AAL_OS_OPS_BEGIN_NOARG(struct aal_mem_info *, get_memory_info)
{
	AAL_OPS_BODY_PTR_NOARG(get_memory_info);
}

AAL_OS_OPS_BEGIN_NOARG(struct aal_cpu_info *, get_cpu_info)
{
	AAL_OPS_BODY_PTR_NOARG(get_cpu_info);
}

AAL_DEV_OPS_BEGIN(unsigned long, map_memory,
                 unsigned long rphys, unsigned long size)
{
	AAL_OPS_BODY(map_memory, rphys, size);
}

AAL_DEV_OPS_BEGIN(int, unmap_memory, unsigned long lphys, unsigned long size)
{
	AAL_OPS_BODY(unmap_memory, lphys, size);
}

AAL_DEV_OPS_BEGIN(void *, map_virtual, unsigned long phys, unsigned long size,
                  void *virtual, int flags)
{
	AAL_OPS_BODY_PTR(map_virtual, phys, size, virtual, flags);
}

AAL_DEV_OPS_BEGIN(int, unmap_virtual, void *virtual, int flags)
{
	AAL_OPS_BODY(unmap_virtual, virtual, flags);
}

#endif
