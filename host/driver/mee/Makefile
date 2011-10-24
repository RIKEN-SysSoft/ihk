KDIR=../target

obj-m += aal_mee.o

aal_mee-objs = aal_mee_driver.o 

EXTRA_CFLAGS = -I$(src)/../../include -I$(src)/../../../ikc/include

.PHONY: clean install

modules:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(RM) .*.cmd *.mod.c *.o *.ko* Module.symvers modules.order -r .tmp*

install:
