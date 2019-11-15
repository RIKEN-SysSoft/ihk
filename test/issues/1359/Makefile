.SUFFIXES:	# Clear suffixes
.SUFFIXES:	.c

CC = gcc

LOCAL_INSTALL=YES

# Please modify this!
AMT_NODE_LIST="4096 0 4096 1"

ifeq ($(LOCAL_INSTALL), YES)
include $(HOME)/.mck_test_config.mk
PREFIX_USR=$(MCK_DIR)
PREFIX_MODULES=$(MCK_DIR)/kmod
PREFIX_IMAGES=$(MCK_DIR)/smp-arm64/kernel
else
UNAME_R=$(shell uname -r)
PREFIX_USR=/usr
PREFIX_MODULES=/lib/modules/$(UNAME_R)/extra/mckernel
PREFIX_IMAGES=/usr/share/mckernel/smp-arm64
endif

CPPFLAGS = -I$(PREFIX_USR)/include -DPREFIX_USR=$(PREFIX_USR) -DPREFIX_MODULES=$(PREFIX_MODULES) -DPREFIX_IMAGES=$(PREFIX_IMAGES) 
CCFLAGS = -g
LDFLAGS = -L$(PREFIX_USR)/lib64 -lihk -Wl,-rpath -Wl,$(PREFIX_USR)/lib64 -lbfd
SRCS = $(shell ls *.c)
EXES = $(SRCS:.c=)
OBJS = $(SRCS:.c=.o)

all: $(EXES)

test: $(EXES)
	sudo ./1359 $(AMT_NODE_LIST)

%: %.o
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CCFLAGS) $(CPPFLAGS) -c $<

clean:
	rm -f core $(EXES) $(OBJS) $(EXESMCK) $(OBJSMCK)

