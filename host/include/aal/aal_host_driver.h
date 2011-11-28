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
	AAL_SPADDR_MIKC_QUEUE_RECV = 2,
	AAL_SPADDR_MIKC_QUEUE_SEND = 3,
};

typedef void *aal_device_t;
typedef void *aal_os_t;

#define aal_device_t_failed(p)   ((p) == NULL)

struct aal_resource;

struct aal_host_interrupt_handler;
struct aal_mem_info;
struct aal_cpu_info;
struct aal_dma_request;
struct aal_dma_channel_info;

struct aal_dma_channel {
	aal_device_t dev;
	int channel;
	struct aal_dma_ops *ops;
};
typedef struct aal_dma_channel *aal_dma_channel_t;

struct aal_dma_ops {
	int (*request)(aal_dma_channel_t, struct aal_dma_request *);
	void (*get_info)(aal_dma_channel_t, struct aal_dma_channel_info *);
};

struct aal_dma_request {
	aal_os_t *src_os;
	unsigned long src_phys;
	aal_os_t *dest_os;
	unsigned long dest_phys;
	unsigned long size;
	
	void (*callback)(void *);
	void *priv;
	unsigned long *notify;
};

struct aal_dma_channel_info {
	unsigned long status;
	unsigned long min_size, max_size;
};

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
	int (*wait_for_status)(aal_os_t, void *, enum aal_os_status, int, int);

	int (*issue_interrupt)(aal_os_t, void *, int, int);

	unsigned long (*map_memory)(aal_os_t, void *,
	                            unsigned long, unsigned long);
	int (*unmap_memory)(aal_os_t, void *, unsigned long, unsigned long);

	int (*register_handler)(aal_os_t, void *, int,
	                        struct aal_host_interrupt_handler *);
	int (*unregister_handler)(aal_os_t, void *, int,
	                          struct aal_host_interrupt_handler *);

	struct aal_mem_info *(*get_memory_info)(aal_os_t, void *);
	struct aal_cpu_info *(*get_cpu_info)(aal_os_t, void *);
	
	int (*get_special_addr)(aal_os_t, void *, enum aal_special_addr_type,
	                        unsigned long *, unsigned long *);

	long (*debug_request)(aal_os_t, void *, unsigned int, unsigned long);
};

struct aal_register_os_data;

struct aal_dma_info {
	unsigned int num_channels, align;
};

struct aal_device_ops {
	int (*init)(aal_device_t, void *);
	int (*exit)(aal_device_t, void *);

	int (*open)(aal_device_t, void *, const void *);
	int (*close)(aal_device_t, void *, const void *);

	int (*create_os)(aal_device_t, void *, unsigned long,
	                 aal_os_t, struct aal_register_os_data *);
	int (*destroy_os)(aal_device_t, void *, aal_os_t, void *);


	unsigned long (*map_memory)(aal_device_t, void *,
	                            unsigned long, unsigned long);
	int (*unmap_memory)(aal_device_t, void *, unsigned long, unsigned long);
	void *(*map_virtual)(aal_device_t, void *, unsigned long, unsigned long,
	                     void *, int);
	int (*unmap_virtual)(aal_device_t, void *, void *, unsigned long);

	long (*debug_request)(aal_device_t, void *, unsigned int,
	                      unsigned long);

	aal_dma_channel_t (*get_dma_channel)(aal_device_t, void *, int);
	int (*get_dma_info)(aal_device_t, void *, struct aal_dma_info *);
};

#define AAL_DEVICE_FLAG_SHARABLE  1
#define AAL_OS_FLAG_SHARABLE  1

#define AAL_MAP_FLAG_NOCACHE  1

#define AAL_IKC_QUEUE_PT_ATTR AAL_MAP_FLAG_NOCACHE

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
unsigned long aal_device_map_memory(aal_device_t dev, unsigned long pa,
                                    unsigned long size);
int aal_device_unmap_memory(aal_device_t dev, unsigned long pa,
                            unsigned long size);
void *aal_device_map_virtual(aal_device_t, unsigned long, unsigned long,
                             void *, int);
int aal_device_unmap_virtual(aal_device_t, void *, unsigned long);

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

struct aal_cpu_info {
	int n_cpus;
	int *hw_ids;
};

struct aal_mem_info *aal_os_get_memory_info(aal_os_t os);
struct aal_cpu_info *aal_os_get_cpu_info(aal_os_t os);

/* Allocate all available cpus */
#define AAL_RESOURCE_CPU_ALL  -1
#define AAL_RESOURCE_MEM_ALL  -1

#define AAL_RESOURCE_FLAG_CPU_SPECIFIED  0x1
#define AAL_RESOURCE_FLAG_MEM_SPECIFIED  0x2

struct aal_resource {
	int flags;

	int cpu_cores; 
	unsigned long mem_size;    /* Memory */
	unsigned long mem_start; /* Start address */
	int cores[0];  /* CPU Cores */
};

struct aal_host_interrupt_handler {
	struct list_head list;
	void (*func)(aal_os_t, void *, void *);
	void *priv;
	/* Filled by AAL */
	aal_os_t os;
	void *os_priv;
};

int aal_os_register_interrupt_handler(aal_os_t os, int itype,
                                      struct aal_host_interrupt_handler *h);
int aal_os_unregister_interrupt_handler(aal_os_t os, int itype,
                                        struct aal_host_interrupt_handler *h);
int aal_os_wait_for_status(aal_os_t os, enum aal_os_status status,
                           int sleepable, int timeout);
int aal_os_get_special_address(aal_os_t os, enum aal_special_addr_type type,
                               unsigned long *pa, unsigned long *size);
unsigned long aal_os_map_memory(aal_os_t os,
                                unsigned long pa, unsigned long size);
int aal_os_unmap_memory(aal_os_t os, unsigned long pa, unsigned long size);

int aal_os_issue_interrupt(aal_os_t os, int cpu, int vector);

aal_device_t aal_os_to_dev(aal_os_t os);
aal_device_t aal_host_find_dev(int index);
aal_os_t aal_host_find_os(int index, aal_device_t dev);

struct aal_os_user_call_handler {
	unsigned int request;
	void *priv;
	long (*func)(aal_os_t os, 
	             unsigned int request, void *priv, unsigned long arg);
};

struct aal_os_user_call {
	struct list_head list;

	int num_handlers;
	struct aal_os_user_call_handler *handlers;
};

int aal_os_register_user_call_handlers(aal_os_t os,
                                       struct aal_os_user_call *);
void aal_os_unregister_user_call_handlers(aal_os_t os,
                                          struct aal_os_user_call *);

int aal_device_get_dma_info(aal_device_t data, struct aal_dma_info *info);
aal_dma_channel_t aal_device_get_dma_channel(aal_device_t data, int channel);
int aal_dma_request(aal_dma_channel_t aal_ch, struct aal_dma_request *req);

#endif
