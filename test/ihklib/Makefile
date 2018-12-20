include $(HOME)/.mck_test_config.mk

CC = gcc

CPPFLAGS = -I$(MCK_DIR)/include -DMCK_DIR=$(MCK_DIR) -DTARGET=$(TARGET) -DARCH=$(ARCH)
CCFLAGS = -Wall -Werror -g
LDFLAGS = -L$(MCK_DIR)/lib64 -lihk -Wl,-rpath -Wl,$(MCK_DIR)/lib64 -lbfd -lpthread
SRCS = $(shell ls *_lin.c)
EXES = $(SRCS:.c=)
OBJS = $(SRCS:.c=.o)

CPPFLAGSMCK = 
CCFLAGSMCK = -Wall -Werror -g
LDFLAGSMCK = 
SRCSMCK = $(shell ls *_mck.c  | grep -v 019)
EXESMCK = $(SRCSMCK:.c=) 
OBJSMCK = $(SRCSMCK:.c=.o)

all: $(EXES) $(EXESMCK)

test::
	for i in {1..24}; do ./run.sh `printf %03d $i`; done

%_lin: %_lin.o
	$(CC) -o $@ $^ $(LDFLAGS)

%_lin.o: %_lin.c
	$(CC) $(CCFLAGS) $(CPPFLAGS) -c $<

%_mck: %_mck.o
	$(CC) -o $@ $^ $(LDFLAGSMCK)

%_mck.o: %_mck.c
	$(CC) $(CCFLAGSMCK) $(CPPFLAGSMCK) -c $<

clean:
	rm -f core $(EXES) $(OBJS) $(EXESMCK) $(OBJSMCK)

