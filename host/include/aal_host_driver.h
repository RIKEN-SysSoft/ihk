#ifndef __HEADER_AAL_HOST_DRIVER_H
#define __HEADER_AAL_HOST_DRIVER_H

typedef void *aal_device_t;
typedef void *aal_os_t;

#define aal_device_t_failed(p)   ((p) == NULL)

struct aal_os_ops {
	int (*open)(aal_os_t, void *, const void *);
	int (*close)(aal_os_t, void *, const void *);

	int (*load_file)(aal_os_t, void *, const char *);
	int (*load_mem)(aal_os_t, void *, const char *, unsigned long,
	                long);
	
	int (*boot)(aal_os_t, void *, int);
	int (*shutdown)(aal_os_t, void *, int);

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

	long (*debug_request)(aal_device_t, void *, unsigned int,
	                      unsigned long);
};

#define AAL_DEVICE_FLAG_SHARABLE  1
#define AAL_OS_FLAG_SHARABLE  1

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

#endif
