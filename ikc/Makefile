IKC_OBJS += queue.o

ifeq ($(CONFIG_AAL),)
DESTOBJS = $(addprefix $O/,$(IKC_OBJS))
IKCBASE = $(CURDIR)
DEPSRCS = $(wildcard $(IKCBASE)/*.c)
ECHO_SUFFIX = [IKC]

$(if $(O),,$(error AAL-IKC Output directory is not specified))
$(if $(TARGET),,$(error Target is not specified))

include $(AALBASE)/Makefile.common

.PHONY: all clean depend

all: depend $(O)/ikc.o

$(O)/ikc.o: $(DESTOBJS)
	$(ld_cmd)

clean:
	$(RM) $(DESTOBJS)

depend: $(O)/Makefile.dep

$(O)/Makefile.dep: $(DEPSRCS)
	$(dep_cmd)

$(O)/%.o: %.c
	$(cc_cmd)
else

include aal/ikc/Makefile.linux

endif
