#ifndef __HEADER_AAL_HOST_DRIVER_H
#define __HEADER_AAL_HOST_DRIVER_H

enum aal_os_status {
	AAL_OS_STATUS_NOT_BOOTED,
	AAL_OS_STATUS_BOOTING,
	AAL_OS_STATUS_BOOTED,    /* OS booted and acked */
	AAL_OS_STATUS_READY,     /* OS is ready and fully functional */
	AAL_OS_STATUS_SHUTDOWN,  /* OS is shutting down */
	AAL_OS_STATUS_STOPPED,   /* OS stopped successfully */
	AAL_OS_STATUS_FAILED,    /* OS panics or failed to boot */
};

enum aal_cpu_status {
	AAL_CPU_STATUS_AVAILABLE,
	AAL_CPU_STATUS_RESERVED,
	AAL_CPU_STATUS_RUNNING,
	AAL_CPU_STATUS_FAILED,
	AAL_CPU_STATUS_NA,       /* N/A. Some hole in apic id, etc. */
};

enum aal_special_addr_type {
	AAL_SPADDR_KMSG = 1,
};

typedef void *aal_device_t;
typedef void *aal_os_t;

#define aal_device_t_failed(p)   ((p) == NULL)

struct aal_resource;

struct aal_os_ops {
	int (*open)(aal_os_t, void *, const void *);
	int (*close)(aal_os_t, void *, const void *);

	int (*load_file)(aal_os_t, void *, const char *);
	int (*load_mem)(aal_os_t, void *, const char *, unsigned long,
	                long);
	
	int (*boot)(aal_os_t, void *, int);
	int (*shutdown)(aal_os_t, void *, int);

	int (*alloc_resource)(aal_os_t, void *, struct aal_resource *);
	enum aal_os_status (*query_status)(aal_os_t, void *);
	int (*issue_interrupt)(aal_os_t, void *, int, int);

	unsigned long (*map_memory)(aal_os_t, void *,
	                            unsigned long, unsigned long);
	int (*unmap_memory)(aal_os_t, void *, unsigned long, unsigned long);
	
	int (*get_special_addr)(aal_os_t, void *, enum aal_special_addr_type,
	                        unsigned long *, unsigned long *);

	long (*debug_request)(aal_os_t, void *, unsigned int, unsigned long);
};

struct aal_register_os_data;

struct aal_device_ops {
	int (*init)(aal_device_t, void *);
	int (*exit)(aal_device_t, void *);

	int (*open)(aal_device_t, void *, const void *);
	int (*close)(aal_device_t, void *, const void *);

	int (*create_os)(aal_device_t, void *, unsigned long,
	                 aal_os_t, struct aal_register_os_data *);
	int (*destroy_os)(aal_device_t, void *, aal_os_t, void *);


	void *(*map_virtual)(aal_device_t, void *, unsigned long, unsigned long,
	                     void *, int);
	int (*unmap_virtual)(aal_device_t, void *, void *, unsigned long);

	long (*debug_request)(aal_device_t, void *, unsigned int,
	                      unsigned long);
};

#define AAL_DEVICE_FLAG_SHARABLE  1
#define AAL_OS_FLAG_SHARABLE  1

#define AAL_MAP_FLAG_NOCACHE  1

struct aal_register_device_data {
	char *name;
	struct aal_device_ops *ops;
	void *priv;
	int flag;
};

struct aal_register_os_data {
	char *name;
	struct aal_os_ops *ops;
	void *priv;
	int flag;
};

aal_device_t aal_register_device(struct aal_register_device_data *);
int aal_unregister_device(aal_device_t);
aal_os_t aal_device_create_os(aal_device_t, unsigned long);

struct aal_mem_region {
	unsigned long start;
	unsigned long size;
};

struct aal_mem_info {
	int n_available;
	int n_fixed;
	int n_mappable;
	struct aal_mem_region *available;
	struct aal_mem_region *fixed;
	struct aal_mem_region *mappable;
};

/* Allocate all available cpus */
#define AAL_RESOURCE_CPU_ALL  -1
#define AAL_RESOURCE_MEM_ALL  -1

struct aal_resource {
	int flags;

	int cores;            /* CPU Cores */
	unsigned long memory; /* Memory */
};

#endif
