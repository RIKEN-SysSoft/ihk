#ifndef HEADER_KNF_H
#define HEADER_KNF_H

#include <sysdeps/knf/mic/micconst.h>
#include <sysdeps/knf/mic/micsboxdefine.h>
#include <sysdeps/knf/mic/mic_dma.h>

void sbox_write(int offset, unsigned int value);
unsigned int sbox_read(int offset);

#endif

