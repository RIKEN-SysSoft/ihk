KDIR=../target.knf

obj-m += aal_knf.o

aal_knf-objs = aal_knf_driver.o knf_device.o knf_dma.o

EXTRA_CFLAGS = -I$(src)/../../include  -I$(src)/../../../ikc/include
EXTRA_CFLAGS += -I$(src)/../../../include

.PHONY: clean install

modules:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(RM) .*.cmd *.mod.c *.o *.ko* Module.symvers modules.order -r .tmp*

install:
