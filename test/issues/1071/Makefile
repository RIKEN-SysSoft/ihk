.SUFFIXES:	# Clear suffixes
.SUFFIXES:	.c

MCKDIR=$(HOME)/project/os/install
CC = gcc

CPPFLAGS = -I$(MCKDIR)/include -DMCKDIR=$(MCKDIR)
CCFLAGS = -g
LDFLAGS = -L$(MCKDIR)/lib -lihk -Wl,-rpath -Wl,$(MCKDIR)/lib -lbfd
SRCS = $(shell ls *.c)
EXES = $(SRCS:.c=)
OBJS = $(SRCS:.c=.o)

all: $(EXES)

test: $(EXES)
	sudo ./1071

%: %.o
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CCFLAGS) $(CPPFLAGS) -c $<

clean:
	rm -f core $(EXES) $(OBJS) $(EXESMCK) $(OBJSMCK)

