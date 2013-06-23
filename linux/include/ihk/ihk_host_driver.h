/**
 * \file ihk_host_driver.h
 * \brief IHK-Host: Structures
 *
 * Copyright (C) 2011-2012 Taku Shimosawa <shimosawa@is.s.u-tokyo.ac.jp>
 */
#ifndef __HEADER_IHK_HOST_DRIVER_H
#define __HEADER_IHK_HOST_DRIVER_H

/** \brief Status of a manycore kernel instance */
enum ihk_os_status {
	IHK_OS_STATUS_NOT_BOOTED,
	IHK_OS_STATUS_BOOTING,
	IHK_OS_STATUS_BOOTED,    /* OS booted and acked */
	IHK_OS_STATUS_READY,     /* OS is ready and fully functional */
	IHK_OS_STATUS_SHUTDOWN,  /* OS is shutting down */
	IHK_OS_STATUS_STOPPED,   /* OS stopped successfully */
	IHK_OS_STATUS_FAILED,    /* OS panics or failed to boot */
};

/** \brief Status of a manycore device */
enum ihk_cpu_status {
	IHK_CPU_STATUS_AVAILABLE,
	IHK_CPU_STATUS_RESERVED,
	IHK_CPU_STATUS_RUNNING,
	IHK_CPU_STATUS_FAILED,
	IHK_CPU_STATUS_NA,       /* N/A. Some hole in apic id, etc. */
};

/** \brief Type of a special address */
enum ihk_special_addr_type {
	IHK_SPADDR_KMSG = 1,
	IHK_SPADDR_MIKC_QUEUE_RECV = 2,
	IHK_SPADDR_MIKC_QUEUE_SEND = 3,
};

/** \brief Type of an IHK device */
typedef void *ihk_device_t;
/** \brief Type of an IHK kernel */
typedef void *ihk_os_t;

#define ihk_device_t_failed(p)   ((p) == NULL)

struct ihk_resource;

struct ihk_host_interrupt_handler;
struct ihk_mem_info;
struct ihk_cpu_info;
struct ihk_dma_request;
struct ihk_dma_channel_info;

/** \brief IHK-Host DMA channel descriptor */
struct ihk_dma_channel {
	ihk_device_t dev;
	void *priv;
	int channel;
	struct ihk_dma_ops *ops;
};
typedef struct ihk_dma_channel *ihk_dma_channel_t;

/** \brief IHK-Host DMA operations */
struct ihk_dma_ops {
	int (*request)(ihk_dma_channel_t, struct ihk_dma_request *);
	void (*get_info)(ihk_dma_channel_t, struct ihk_dma_channel_info *);
};

/** \brief IHK-Host DMA request structure */
struct ihk_dma_request {
	/** \brief Kernel where the source memory area resides */
	ihk_os_t src_os;
	/** \brief Source physical address */
	unsigned long src_phys;
	/** \brief Kernel where the destination memory area resides */
	ihk_os_t dest_os;
	/** \brief Destination physical address */
	unsigned long dest_phys;
	/** \brief Size in byte */
	unsigned long size;
	
	/**
	 * \brief Function to be called back when the operation is done.
	 *
	 * If callback is NULL, nothing is called.
	 * If both callback and notify are not NULL, callback is used and 
	 * nothing is written to notify.
	 */
	void (*callback)(void *);
	/** \brief Parameter to the callback function, or value to write to
	 *         notification memory area */
	void *priv;
	/** \brief Kernel where the notification memory area resides */
	ihk_os_t notify_os;
	/**
	 * \brief Physical address of the notification memory area to which
	 *        a value set in priv is written when the operation is done.
	 */
	unsigned long *notify;
};

/** \brief Information of a DMA channel */
struct ihk_dma_channel_info {
	/** \brief Status of the channel */
	unsigned long status;
	/** \brief Minimum size to transfer at a time */
	unsigned long min_size;
	/** \brief Maximum size to transfer at a time */
	unsigned long max_size;
};

/** \brief IHK-Host driver handlers for OS operations */
struct ihk_os_ops {
	/** \brief When a user tries to open an OS device file 
	 *
	 *  \param os     IHK OS structure
	 *  \param priv   Private pointer related to os
	 *  \param file   Identifier of the device file
	 **/
	int (*open)(ihk_os_t os, void *priv, const void *file);
	/** \brief When a user tries to close an OS device file 
	 *
	 *  \param file   Identifier of the device file
	 **/
	int (*close)(ihk_os_t os, void *priv, const void *file);

	/** \brief Load a kernel image for the kernel instance from a file
	 *
	 *  \param filename  File name of the kernel image
	 **/
	int (*load_file)(ihk_os_t os, void *priv, const char *filename);

	/** \brief Load a kernel image for the kernel instance from a buffer
	 *
	 *  The called function should either write the whole thing in buffer
	 *  or report an error (by returning a negative value),
	 *  \param buf    Buffer that contains (a part of) a kernel image
	 *  \param size   Size of the buffer
	 *  \param offset Offset to write
	 **/
	int (*load_mem)(ihk_os_t os, void *priv, const char *buf,
	                unsigned long size, long offset);
	
	/** \brief Boot a kernel
	 *
	 *  \param flag  Unused
	 **/
	int (*boot)(ihk_os_t, void *, int flag);
	/** \brief Shutdown a kernel
	 *
	 *  \param flag  Unused
	 **/
	int (*shutdown)(ihk_os_t, void *, int flag);

	/** \brief Allocate a resource
	 *
	 *  \param resource Resource to allocate
	 **/
	int (*alloc_resource)(ihk_os_t, void *, struct ihk_resource *resource);

	/** \brief Query the status of a kernel
	 *
	 *  \return Status of the kernel
	 **/
	enum ihk_os_status (*query_status)(ihk_os_t, void *);

	/** \brief Query free memory of a kernel
	 *
	 *  \return Number of free pages on MIC
	 **/
	int (*query_free_mem)(ihk_os_t, void *);

	/** \brief Wait for the status of a kernel to change
	 *
	 *  \param status  The desired status. This function should wait 
	 *                 until the kernel changes its status to this value.
	 *  \param sleepable Whether the function may sleep or not.
	 *  \param timeout Maximum time to wait in milliseconds.
	 **/
	int (*wait_for_status)(ihk_os_t, void *, enum ihk_os_status status,
	                       int sleepable, int timeout);

	/** \brief Issue an interrupt to a kernel
	 *
	 *  \param status  The desired status. This function should wait 
	 *                 until the kernel changes its status to this value.
	 *  \param sleepable Whether the function may sleep or not.
	 *  \param timeout Maximum time to wait in milliseconds.
	 **/
	int (*issue_interrupt)(ihk_os_t, void *, int cpu, int vector);

	/** \brief Set a kernel command line paramter
	 *
	 * \param buf Parameter string */
	int (*set_kargs)(ihk_os_t, void *, char *buf);

	/** \note Obsolete. */
	unsigned long (*map_memory)(ihk_os_t, void *,
	                            unsigned long, unsigned long);
	/** \note Obsolete. */
	int (*unmap_memory)(ihk_os_t, void *, unsigned long, unsigned long);

	/**
	 * \brief Register a handler for an interrupt
	 *
	 * \param itype Type of an interrupt which the handler handles.
	 * \param h     Descriptor of the handler to register
	 */
	int (*register_handler)(ihk_os_t, void *, int itype,
	                        struct ihk_host_interrupt_handler *h);
	/**
	 * \brief Unregister a handler for the interrupt
	 *
	 * \param itype Type of an interrupt
	 * \param h     Descriptor of the handler to unregister
	 */
	int (*unregister_handler)(ihk_os_t, void *, int,
	                          struct ihk_host_interrupt_handler *);

	/** \brief Retrieve memory information for the OS instance */
	struct ihk_mem_info *(*get_memory_info)(ihk_os_t, void *);
	/** \brief Retrieve processor information for the OS instance */
	struct ihk_cpu_info *(*get_cpu_info)(ihk_os_t, void *);
	
	/**
	 * \brief Get a special address of the OS instance
	 *
	 * This function also retrieves a size if applicable.
	 * \param type Type of the address to retrieve
	 * \param addr Pointer to a variable to store the address
	 * \param size Pointer to a variable to store the size
	 */
	int (*get_special_addr)(ihk_os_t, void *,
	                        enum ihk_special_addr_type type,
	                        unsigned long *addr, unsigned long *size);
	/**
	 * \brief Handle an ioctl request with the request number
	 *  in the debug region.
	 *
	 * \param request Request number (the second argument of ioctl)
	 * \param arg     Argument (the third argument of ioctl)
	 */ 
	long (*debug_request)(ihk_os_t, void *, unsigned int request,
	                      unsigned long arg);
};

struct ihk_register_os_data;

/** \brief Information structure for the DMA engine */
struct ihk_dma_info {
	/* \brief Number of channels available */
	unsigned int num_channels;
	/* \brief Alignment required for the arguments of DMA engine */
	unsigned int align;
};

/** \brief IHK-Host driver handlers for device operations */
struct ihk_device_ops {
	/**
	 * \brief Initialize a device.
	 *
	 * \param ihk_dev IHK Device structure
	 * \param priv    Device-private pointer related to the device
	 *  */
	int (*init)(ihk_device_t, void *);
	/** \brief Finalize a device */
	int (*exit)(ihk_device_t, void *);

	/**
	 * \brief Open a device
	 *
	 * \param file Identifier of the device file of this device
	 */
	int (*open)(ihk_device_t, void *, const void *file);
	/**
	 * \brief Close a device
	 *
	 * \param file Identifier of the device file of this device
	 */
	int (*close)(ihk_device_t, void *, const void *file);

	/**
	 * \brief Create an OS kernel instance
	 *
	 * The function is called when a user requests the device to
	 * create a new OS instance.
	 * The function must fill the os_data structure.
	 * \param arg     Unused
	 * \param os      A new IHK OS instance
	 * \param os_data OS registering information structure
	 */
	int (*create_os)(ihk_device_t, void *, unsigned long arg,
	                 ihk_os_t, struct ihk_register_os_data *);
	/**
	 * \brief Destroy an OS instance
	 *
	 * \param os      OS instance to be destroyed
	 * \param os_priv A driver-specific data related the OS instance
	 */
	int (*destroy_os)(ihk_device_t, void *, ihk_os_t os, void *os_priv);

	/**
	 * \brief Map a physical memory area to the host physical memory
	 *
	 * \param addr Physical address in the manycore device
	 * \param size Size of the area to map
	 */
	unsigned long (*map_memory)(ihk_device_t, void *,
	                            unsigned long, unsigned long);
	/**
	 * \brief Unmap a host physical memory that mapped to the 
	 *         memory of the manycore device
	 *
	 * \param addr Physical address in the manycore device
	 * \param size Size of the area to map
	 */
	int (*unmap_memory)(ihk_device_t, void *, unsigned long addr,
	                    unsigned long size);

	/**
	 * \brief Map a physical memory that is mapped to a memory region
	 *        of the manycore device to the kernel virtual memory
	 *
	 * \note  The use of this function is not recommended
	 * \param pa Host physical address
	 * \param size Size of the memory region to map
	 * \param virt Desired virtual address. If it is set NULL, the kernel
	 *             automatically designate.
	 * \param flag Indicates the attribute of mapping
	 * \return Kernel virtual address.
	 */
	void *(*map_virtual)(ihk_device_t, void *, unsigned long pa,
	                     unsigned long size, void *virt, int flag);
	/**
	 * \brief Unmap a virtual memory region mapped by 
	 *        the map_virtual function.
	 *
	 * \note  The use of this function is not recommended
	 * \param virt Pointer to the region to unmap
	 * \param size Size of the memory region to unmap
	 */
	int (*unmap_virtual)(ihk_device_t, void *, void *virt,
	                     unsigned long size);

	/**
	 * \brief Handle an ioctl request with the request number
	 *  in the debug region.
	 *
	 * \param request Request number (the second argument of ioctl)
	 * \param arg     Argument (the third argument of ioctl)
	 */ 
	long (*debug_request)(ihk_device_t, void *, unsigned int request,
	                      unsigned long arg);

	/**
	 * \brief Get a DMA channel of the device
	 *
	 * \param channel Channel number of the device
	 * \return Descriptor for the DMA channel. NULL if an error occurs.
	 */
	ihk_dma_channel_t (*get_dma_channel)(ihk_device_t, void *, int channel);
	/**
	 * \brief Get information of the DMA engine of the device
	 *
	 * \param info Structure to store DMA information
	 */
	int (*get_dma_info)(ihk_device_t, void *, struct ihk_dma_info *info);
};

/**
 * \brief Denote that the device file of this device may be opened by 
 *        multiple processes at the same time.
 *
 * This constant is used in the flag member of ihk_register_device_data. 
 */
#define IHK_DEVICE_FLAG_SHARABLE  1
/**
 * \brief Denote that the device file of this kernel may be opened by 
 *        multiple processes at the same time.
 *
 * This constant is used in the flag member of ihk_register_os_data. 
 */
#define IHK_OS_FLAG_SHARABLE  1

/** \brief Caching of the mapped area must be disabled. */
#define IHK_MAP_FLAG_NOCACHE  1

/** \brief Default IKC queue mapping attribute */
#define IHK_IKC_QUEUE_PT_ATTR IHK_MAP_FLAG_NOCACHE

/** \brief Device registration structure */
struct ihk_register_device_data {
	/** \brief Name of the device */
	char *name;
	/** \brief Device operation handlers */
	struct ihk_device_ops *ops;
	/**
	 * \brief Driver-specific private data related to the device
	 *
	 * The value is always passed as an argument from IHK-Host when it calls
	 * operation handlers.
	 */
	void *priv;
	/** \brief Attribute of the device */
	int flag;
};

/** \brief OS registration structure */
struct ihk_register_os_data {
	/** \brief Name of the OS kernel */
	char *name;
	/** \brief OS kernel operation handlers */
	struct ihk_os_ops *ops;
	/**
	 * \brief Driver-specific private data related to the OS kernel
	 *
	 * The value is always passed as an argument from IHK-Host when it calls
	 * operation handlers.
	 */
	void *priv;
	/** \brief Attribute of the device */
	int flag;
};

/**
 * \brief Register a device to the IHK-Host core.
 *
 * This function registers a device to the IHK-Host core, and has IHK-Host
 * create a device file corresponding to the device.
 * It is typically called by IHK device drivers.
 */
ihk_device_t ihk_register_device(struct ihk_register_device_data *);
/**
 * \brief Unregister a device to the IHK-Host core.
 *
 * This function unregisters a device from the IHK-Host core.
 * It is typically called by IHK device drivers on their unloading, etc.
 */
int ihk_unregister_device(ihk_device_t);

/**
 * \brief Create an OS instance in the device 
 *
 * \param device IHK device structure
 * \param arg    Unused
 * \return IHK OS structure
 */
ihk_os_t ihk_device_create_os(ihk_device_t device, unsigned long arg);

/**
 * \brief Map a physical memory area of the device to be visible from the host
 *        (allowing access by some physical addresses)
 *
 * \param dev    IHK device structure
 * \param pa     Starting physical address in the device to map
 * \param size   Size of the memory area to map
 * \return Starting physical address in the host. 0 if failed.
 */
unsigned long ihk_device_map_memory(ihk_device_t dev, unsigned long pa,
                                    unsigned long size);
/**
 * \brief Unmap a physical memory area of the device to be visible from the host
 *        (allowing access by some physical addresses)
 *
 * \param dev    IHK device structure
 * \param pa     Starting physical address in the host
 * \param size   Size of the mapped memory area 
 */
int ihk_device_unmap_memory(ihk_device_t dev, unsigned long pa,
                            unsigned long size);
void *ihk_device_map_virtual(ihk_device_t, unsigned long, unsigned long,
                             void *, int);
int ihk_device_unmap_virtual(ihk_device_t, void *, unsigned long);

/** \brief Structure representing a memory region */
struct ihk_mem_region {
	/** \brief Start address */
	unsigned long start;
	/** \brief Size of the region */
	unsigned long size;
};

/** \brief Memory information used in IHK functions */
struct ihk_mem_info {
	/** \brief Number of memory regions available for use */
	int n_available;
	/** \brief Number of memory regions mapped (visible to the host) 
	 *         in a fixed manner */
	int n_fixed;
	/** \brief Number of memory regions that can be mapped to the host
	 *         (dynamically). */
	int n_mappable;
	/** \brief Array of memory regions available for use */
	struct ihk_mem_region *available;
	/** \brief Array of memory regions mapped fixedly */
	struct ihk_mem_region *fixed;
	/** \brief Array of memory regions mappable to the host */
	struct ihk_mem_region *mappable;
};

/** \brief CPU information used in IHK functions */
struct ihk_cpu_info {
	/** \brief Number of CPU cores */
	int n_cpus;
	/** \brief Array of the hardware ID of the CPU cores */
	int *hw_ids;
};

/** \brief Get information of memory which the OS kernel uses */
struct ihk_mem_info *ihk_os_get_memory_info(ihk_os_t os);
/** \brief Get information of CPU cores which the OS kernel uses */
struct ihk_cpu_info *ihk_os_get_cpu_info(ihk_os_t os);

/** \brief Denote to allocate all the available cpus */
#define IHK_RESOURCE_CPU_ALL  -1
/** \brief Denote to allocate all the available memory */
#define IHK_RESOURCE_MEM_ALL  -1

/** \brief Specify the certain CPU cores instead of choosing automatically */
#define IHK_RESOURCE_FLAG_CPU_SPECIFIED  0x1
/** \brief Specify the certain memory area instead of choosing automatically */
#define IHK_RESOURCE_FLAG_MEM_SPECIFIED  0x2

/** \brief Resource information used in IHK functions */
struct ihk_resource {
	/** \brief Some flags of how the desired resource is specified */
	int flags;

	/** \brief Number of CPU cores */
	int cpu_cores; 
	/** \brief Size of the memory */
	unsigned long mem_size;
	/**
	 * \brief Start address of the memory.
	 *
	 * If IHK_RESOURCE_FLAG_MEM_SPECIFIED is set in "flags", it means that
	 * the requester requests the certain memory region from mem_start
	 * to mem_start + mem_size.
	 * Otherwise, this field is stored by the driver to indicate where
	 * the memory is allocated
	 */
	unsigned long mem_start;
	/**
	 * \brief ID of CPU cores
	 *
	 * If IHK_RESOURCE_FLAG_CPU_SPECIFIED is set in "flags", it means that
	 * the requester requests the certain set of CPU cores in the "cores"
	 * array. 
	 * Otherwise, this field is stored by the driver to indicate which cores
	 * are allocated.
	 */
	int cores[0];
};

/** \brief Desciptor of the interrupt handlers */
struct ihk_host_interrupt_handler {
	/** \brief List head. Internal use. */
	struct list_head list;
	/** \brief Pointer to the handler */
	void (*func)(ihk_os_t os, void *os_priv, void *priv);
	/** \brief Private value passed to the handler as the third argument */
	void *priv;
	/** \brief Related OS instance. Internal use. */
	ihk_os_t os;
	/** \brief Private data for the OS instance. Internal use. */
	void *os_priv;
};

/**
 * \brief Register an handler for the specified type of interrupts
 *        from the OS instance.
 *
 * \param itype  Type of interrupt
 * \param h      Descriptor of the interrupt handler to register
 */
int ihk_os_register_interrupt_handler(ihk_os_t os, int itype,
                                      struct ihk_host_interrupt_handler *h);
/**
 * \brief Unregister an interrupt handler
 *
 * \param itype  Type of interrupt
 * \param h      Descriptor of the interrupt handler to unregister
 */
int ihk_os_unregister_interrupt_handler(ihk_os_t os, int itype,
                                        struct ihk_host_interrupt_handler *h);
/**
 * \brief Wait for an OS instance to change its stage
 *
 * \param status    New status to wait
 * \param sleepable Whether the function may sleep or not
 * \param timeout   Timeout of waiting in ms.
 */
int ihk_os_wait_for_status(ihk_os_t os, enum ihk_os_status status,
                           int sleepable, int timeout);
/**
 * \brief Get a special address of the OS instance.
 *
 * \param type      Type of the special address
 * \param pa        Result in physical address
 * \param size      Size of the queried memory region if applicable
 */
int ihk_os_get_special_address(ihk_os_t os, enum ihk_special_addr_type type,
                               unsigned long *pa, unsigned long *size);
unsigned long ihk_os_map_memory(ihk_os_t os,
                                unsigned long pa, unsigned long size);
int ihk_os_unmap_memory(ihk_os_t os, unsigned long pa, unsigned long size);

/**
 * \brief Issue an interrupt to the OS instance
 *
 * \param cpu    Processor ID to interrupt
 * \param vector Vector number used for the interrupt
 */
int ihk_os_issue_interrupt(ihk_os_t os, int cpu, int vector);

/**
 * \brief Get the device instance from a OS instance.
 *
 * \param os     OS instance
 */
ihk_device_t ihk_os_to_dev(ihk_os_t os);

/**
 * \brief Get the device instance of the specified index
 *
 * \param index  Index number
 */
ihk_device_t ihk_host_find_dev(int index);

/**
 * \brief Get the OS instance of the specified index,
 *
 * \param index  Index number
 * \param dev    Optional. Related device instance.
 *               If the device of the found OS does not match this,
 *               this function returns -ENOENT.
 *               If this parameter is not set, this checking is omitted.
 */
ihk_os_t ihk_host_find_os(int index, ihk_device_t dev);

/**
 * \brief Set the user data to a OS instance.
 *
 * \param os     OS instance
 * \param data   user data
 */
void ihk_host_os_set_usrdata(ihk_os_t os, void *data);

/**
 * \brief Get the user data from a OS instance.
 *
 * \param os     OS instance
 */
void *ihk_host_os_get_usrdata(ihk_os_t);

/**
 * \brief Descriptor of the handler for a ioctl request to the OS device file.
 */
struct ihk_os_user_call_handler {
	/** \brief Request number (ioctl) */
	unsigned int request;
	/** \brief Passed as the third argument to the handler */
	void *priv;
	/** \brief Handler */
	long (*func)(ihk_os_t os, 
	             unsigned int request, void *priv, unsigned long arg);
};

/** \brief Descriptor of the additional handlers for ioctl requests */
struct ihk_os_user_call {
	/** \brief List. Internal use */
	struct list_head list;

	/** \brief Number of handlers in the "handlers" array */
	int num_handlers;
	/** \brief Array of handlers */
	struct ihk_os_user_call_handler *handlers;
};

/** \brief Register new ioctl handlers for the specified OS instance */
int ihk_os_register_user_call_handlers(ihk_os_t os,
                                       struct ihk_os_user_call *);
/** \brief Unregister the ioctl handlers */
void ihk_os_unregister_user_call_handlers(ihk_os_t os,
                                          struct ihk_os_user_call *);

/** \brief Get a DMA information of the device */
int ihk_device_get_dma_info(ihk_device_t data, struct ihk_dma_info *info);
/** \brief Get the DMA channel descriptor for the specified channel */
ihk_dma_channel_t ihk_device_get_dma_channel(ihk_device_t data, int channel);
/** \brief Request a DMA opertation on the DMA channel */
int ihk_dma_request(ihk_dma_channel_t ihk_ch, struct ihk_dma_request *req);

#endif
