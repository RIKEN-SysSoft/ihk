.SUFFIXES:	# Clear suffixes
.SUFFIXES:	.c 

CC = gcc

CPPFLAGS = -I$(HOME)/project/os/install/include
CCFLAGS = -g
LDFLAGS = -L$(HOME)/project/os/install/lib -lihk -Wl,-rpath -Wl,$(HOME)/project/os/install/lib -lbfd
SRCS = $(shell ls ihklib*lin.c) udev.c
EXES = $(SRCS:.c=)
OBJS = $(SRCS:.c=.o)

CPPFLAGSMCK = 
CCFLAGSMCK = -g
LDFLAGSMCK = 
SRCSMCK = $(shell ls ihklib*mck.c)
EXESMCK = $(SRCSMCK:.c=) 
OBJSMCK = $(SRCSMCK:.c=.o)

all: $(EXES) $(EXESMCK)

udev: udev.c
	gcc -g -O0 -o udev udev.c -ludev

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

