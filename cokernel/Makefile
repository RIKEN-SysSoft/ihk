IHKBASE = $(CURDIR)
ECHO_SUFFIX = [IHK]

IHK_OBJS += string.o vsprintf.o abort.o page_alloc.o mikc.o ikc.o gencore.o bitops.o bitmap.o

DESTOBJS = $(addprefix $O/,$(IHK_OBJS))
SUBOPTS = CC=$(CC) LD=$(LD) V=$(V) TARGET=$(TARGET) IHKBASE=$(IHKBASE)

$(if $(O),,$(error IHK Output directory is not specified))
$(if $(TARGET),,$(error Target is not specified))

CFLAGS += -I$(SRC)/include -D__KERNEL__

BUILD_IHK_COKERNEL ?= .
include $(BUILD_IHK_COKERNEL)/Makefile.common

IHK_SRCDIRS += $(SRC)/../lib $(TARGETDIR)
DEPSRCS = $(foreach d,$(IHK_SRCDIRS),$(wildcard $(d)/*.c $(d)/*.S))

$(foreach d,$(IHK_SRCDIRS),$(eval $(call make_implicit_src,$(d))))

.PHONY: all clean depend prepare

all: depend $(O)/ihk.o

$(O)/ihk.o: $(DESTOBJS) $(O)/ikc/ikc.o
	$(ld_cmd)

$(O)/ikc/ikc.o: FORCE
	$(call echo_cmd,BUILD IKC,$(TARGET))
	@mkdir -p $(O)/ikc
	@$(submake) -C $(CURDIR)/../ikc $(SUBOPTS) O=$(O)/ikc prepare
	@$(submake) -C $(CURDIR)/../ikc $(SUBOPTS) O=$(O)/ikc

clean:
	@$(submake) -C $(CURDIR)/../ikc $(SUBOPTS) O=$(O)/ikc clean
	$(rm_cmd) $(DESTOBJS) $(O)/Makefile.dep

depend: $(O)/Makefile.dep

$(O)/Makefile.dep:
	$(call dep_cmd,$(DEPSRCS))

prepare:
	@$(RM) $(O)/Makefile.dep

FORCE:

-include $(O)/Makefile.dep
