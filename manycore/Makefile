CFLAGS += -Wall -O -Iinclude -I../generic/include -Wno-unused-function
AALBASE = $(CURDIR)
ECHO_SUFFIX = [AAL]

AAL_OBJS += string.o vsprintf.o abort.o

DESTOBJS = $(addprefix $O/,$(AAL_OBJS))

$(if $(O),,$(error AAL Output directory is not specified))

include Makefile.common
include $(TARGET)/Makefile.build

AAL_SRCDIRS += generic $(TARGET)
DEPSRCS = $(foreach d,$(AAL_SRCDIRS),$(wildcard $(d)/*.c $(d)/*.S))

$(foreach d,$(AAL_SRCDIRS),$(eval $(call make_implicit_src,$(d))))

.PHONY: all clean depend

all: depend $(O)/aal.o

$(O)/aal.o: $(DESTOBJS)
	$(ld_cmd)

clean:
	$(RM) $(DESTOBJS)

depend: $(O)/Makefile.dep

$(O)/Makefile.dep: $(DEPSRCS)
	$(dep_cmd)
