.SUFFIXES:	# Clear suffixes
.SUFFIXES:	.c

MCKDIR=$(HOME)/ihk+mckernel
MCKSRC=$(HOME)/src/mckernel

CC = gcc

CPPFLAGS = -I$(MCKDIR)/include -I$(MCKSRC)/ihk/linux/include -DMCKDIR=$(MCKDIR)
CCFLAGS = -g
LDFLAGS = -L$(MCKDIR)/lib64 -lihk -Wl,-rpath -Wl,$(MCKDIR)/lib64
SRCS = $(shell ls *.c)
EXES = $(SRCS:.c=)
OBJS = $(SRCS:.c=.o)

all: $(EXES)

test: $(EXES)
	sh ./C1509.sh

%: %.o
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CCFLAGS) $(CPPFLAGS) -c $<

clean:
	rm -f core $(EXES) $(OBJS) $(EXESMCK) $(OBJSMCK)

