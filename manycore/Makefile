AALBASE = $(CURDIR)
ECHO_SUFFIX = [AAL]

AAL_OBJS += string.o vsprintf.o abort.o page_alloc.o mikc.o ikc.o

DESTOBJS = $(addprefix $O/,$(AAL_OBJS))
SUBOPTS = CC=$(CC) LD=$(LD) V=$(V) TARGET=$(TARGET) AALBASE=$(AALBASE)

$(if $(O),,$(error AAL Output directory is not specified))
$(if $(TARGET),,$(error Target is not specified))

include Makefile.common

AAL_SRCDIRS += generic $(TARGET)
DEPSRCS = $(foreach d,$(AAL_SRCDIRS),$(wildcard $(d)/*.c $(d)/*.S))

$(foreach d,$(AAL_SRCDIRS),$(eval $(call make_implicit_src,$(d))))

.PHONY: all clean depend prepare

all: depend $(O)/aal.o

$(O)/aal.o: $(DESTOBJS) $(O)/ikc/ikc.o
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
