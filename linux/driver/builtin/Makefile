KDIR=../target

obj-m += ihk_builtin.o

ihk_builtin-objs = ihk_builtin_driver.o dmacore.o dmacore_intr.o

EXTRA_CFLAGS = -I$(src)/../../include -I$(src)/../../../ikc/include

.PHONY: clean install

modules:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(RM) .*.cmd *.mod.c *.o *.ko* Module.symvers modules.order -r .tmp*

install:
