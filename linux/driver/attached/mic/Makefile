KDIR ?= /lib/modules/`uname -r`/build
src ?= $(shell pwd)

obj-m += ihk_mic.o

ihk_mic-objs = ihk_mic_driver.o mic_device.o mic_dma.o

EXTRA_CFLAGS = -I$(src)/../../../include -I$(src)/../../../../include -I$(src)/../../../../ikc/include -I$(src)/include

KBUILD_EXTRA_SYMBOLS=$(src)/../../../core/Module.symvers

.PHONY: clean install

modules:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(RM) .*.cmd *.mod.c *.o *.ko* Module.symvers modules.order -r .tmp*

install:
