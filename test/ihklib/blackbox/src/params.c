#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "util.h"
#include "params.h"

struct params params;

void params_getopt(int argc, char **argv)
{
	int opt;
	int additional = 0;

	opterr = 0;
	while ((opt = getopt(argc, argv, "u:g:")) != -1) {
		switch (opt) {
		case 'u':
			params.uid = atoi(optarg);
			break;
		case 'g':
			params.gid = atoi(optarg);
			break;
		default: /* ignoring '?' because it could be parsed elsewhere */
			additional = 1;
			break;
		}
		if (additional) {
			optind--;
			break;
		}
	}
	opterr = 1;
}
