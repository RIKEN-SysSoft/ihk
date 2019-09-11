.SUFFIXES:	# Clear suffixes
.SUFFIXES:	.c

include $(HOME)/.mck_test_config.mk

CC = gcc

CPPFLAGS = -I$(MCK_DIR)/include -DMCK_DIR=$(MCK_DIR)
CCFLAGS = -g
LDFLAGS = -L$(MCK_DIR)/lib64 -lihk -Wl,-rpath -Wl,$(MCK_DIR)/lib64 -lbfd
SRCS = $(shell ls *.c)
EXES = $(SRCS:.c=)
OBJS = $(SRCS:.c=.o)

all: $(EXES)

test: $(EXES)
	sudo ./1359

%: %.o
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CCFLAGS) $(CPPFLAGS) -c $<

clean:
	rm -f core $(EXES) $(OBJS) $(EXESMCK) $(OBJSMCK)

