CFLAGS=-Wall -O
TARGET=aaltest aalostest memdump pagetable maccess memdump-mic knf_ctrl

AALBASE ?= ../../aal

all: aal $(TARGET)

aal:
	rm -f aal; ln -s $(AALBASE)/host/include/aal aal

clean:
	$(RM) $(TARGET) *.o

.PHONY: all clean


