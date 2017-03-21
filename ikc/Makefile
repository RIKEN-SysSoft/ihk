IKC_OBJS += queue.o master.o

ifeq ($(CONFIG_IHK),)
DESTOBJS = $(addprefix $O/,$(IKC_OBJS))
IKCBASE = $(CURDIR)
DEPSRCS = $(wildcard $(IKCBASE)/*.c)
ECHO_SUFFIX = [IKC]

IKC_OBJS += manycore.o

$(if $(O),,$(error IHK-IKC Output directory is not specified))
$(if $(TARGET),,$(error Target is not specified))

BUILD_IHK_COKERNEL ?= $(IHKBASE)
include $(BUILD_IHK_COKERNEL)/Makefile.common

.PHONY: all clean depend

all: depend $(O)/ikc.o

$(O)/ikc.o: $(DESTOBJS)
	$(ld_cmd)

clean:
	$(rm_cmd) $(DESTOBJS)

depend: $(O)/Makefile.dep

prepare:
	@$(RM) $(O)/Makefile.dep

$(O)/Makefile.dep:
	$(call dep_cmd,$(DEPSRCS))

$(O)/%.o: %.c
	$(cc_cmd)
else

IKC_OBJS += linux.o

include $(src)/Makefile.linux

endif
