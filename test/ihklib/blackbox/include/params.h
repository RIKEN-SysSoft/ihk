#ifndef __PARAMS_H_INCLUDED__
#define __PARAMS_H_INCLUDED__

#include <unistd.h>
#include <sys/types.h>

struct params {
	uid_t uid;
	gid_t gid;
};

extern struct params params;

void params_getopt(int argc, char **argv);

#endif
