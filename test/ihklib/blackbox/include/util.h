#ifndef __UTIL_H_INCLUDED__
#define __UTIL_H_INCLUDED__

#include <stdio.h>

//#define DEBUG

#ifdef DEBUG
#define dprintf(...) do {			 \
	char msg[1024];			 \
	sprintf(msg, __VA_ARGS__);		 \
	printf("[ DEBUG] %s: %s", __func__, msg); \
} while (0)
#else
#define dprintf(...) do {  } while (0)
#endif

#define Q(x) #x
#define QUOTE(x) Q(x)

#endif
