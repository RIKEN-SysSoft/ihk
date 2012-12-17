#CC=/usr/linux-k1om-4.7/bin/x86_64-k1om-linux-gcc
CC=gcc
CFLAGS=-Wall -O
TARGET=ihktest ihkostest memdump pagetable maccess memdump-mic

IHKBASE ?= ../../../ihk

all: ihk $(TARGET)

ihk:
	rm -f ihk; ln -s $(IHKBASE)/linux/include/ihk ihk

clean:
	$(RM) $(TARGET) *.o

.PHONY: all clean


