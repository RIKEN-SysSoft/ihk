#ifndef HEADER_MIC_H
#define HEADER_MIC_H

#include <sysdeps/mic/mic/micconst.h>
#include <sysdeps/mic/mic/micsboxdefine.h>
#include <sysdeps/mic/mic/mic_dma.h>

void sbox_write(int offset, unsigned int value);
unsigned int sbox_read(int offset);

#endif

