#ifndef __OKNG_H_INCLUDED__
#define __OKNG_H_INCLUDED__

#include <stdio.h>

#define _OKNG(verb, jump, cond, fmt, args...) do {	\
	if (cond) {					\
		if (verb)				\
			printf("[  OK  ] " fmt, ##args);	\
	} else {					\
		printf("[  NG  ] " fmt, ##args);		\
		if (jump)				\
			goto out;			\
	}						\
} while (0)

#define OKNG(args...) _OKNG(1, 1, ##args)
#define NG(args...) _OKNG(0, 1, ##args)
#define OKNGNOJUMP(args...) _OKNG(1, 0, ##args)

#define INFO(fmt, args...) do { \
	printf("[ INFO ] " fmt, ##args); \
} while (0)

#define START(fmt, args...) do { \
	printf("[ START] " fmt, ##args); \
} while (0)

#define INTERR(cond, fmt, args...) do {	 \
	if (cond) {			 \
		char msg[4096];			 \
		sprintf(msg, fmt, ##args);		 \
		printf("[INTERR] %s:%d %s", __FILE__, __LINE__, msg);	\
		ret = 1;					\
		goto out;					\
	} \
} while (0)

#endif
