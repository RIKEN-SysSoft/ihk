OBJS += setup.o cpu.o sfi.o interrupt.o memory.o

include Makefile.config
include ../Makefile.aal

%.o: ../x86_common/%.c
	$(cc_cmd)

%.o: ../x86_common/%.S
	$(cc_cmd)
