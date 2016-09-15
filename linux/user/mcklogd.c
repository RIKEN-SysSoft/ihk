#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <getopt.h>
#include <signal.h>
#include <syslog.h>

#define FLAG_DAEMONIZE		 1
#define FLAG_KILL					2

#define KPRINTF_LOCAL_BUF_LEN 1024
int main(int argc, char **argv)
{
	int opt;
	int flag = 0;
	int nochdir = 1;
	int noclose = 0;
	int interval = 1;
	int facility = LOG_LOCAL6;
	int i;
	char buf[512];
	char command[512];
	char *envptr;
	FILE *fp;

	struct {
		char name[12];
		int code;
	} facility_list[8] = {
		{"LOG_LOCAL0", LOG_LOCAL0},
		{"LOG_LOCAL1", LOG_LOCAL1},
		{"LOG_LOCAL2", LOG_LOCAL2},
		{"LOG_LOCAL3", LOG_LOCAL3},
		{"LOG_LOCAL4", LOG_LOCAL4},
		{"LOG_LOCAL5", LOG_LOCAL5},
		{"LOG_LOCAL6", LOG_LOCAL6},
		{"LOG_LOCAL7", LOG_LOCAL7}
	};
	const struct option longopt[] = {
		{"help", no_argument, NULL, '?'},
		{"daemonize", no_argument, NULL, 'd'},
		{"kill", no_argument, NULL, 'k'},
		{"interval", required_argument, NULL, 'i'},
		{NULL, 0, NULL, 0}
	};

	flag = FLAG_DAEMONIZE;
	
	while ((opt = getopt_long(argc, argv, "dki:f:", longopt, NULL)) != -1) {
		switch (opt) {
		case 'i':
			interval = atoi(optarg);
			if (interval <= 0) interval = 1;
			break;
		case 'd':
			flag = FLAG_DAEMONIZE;
			break;
		case 'k':
			flag = FLAG_KILL;
			break;
		case 'f':
			/**
			 * mcklogd[PID]: log message
			 * default facility: LOG_LOCAL6 (/var/log/local6)
			 */
			for (i = 0; i < 8; i++) {
				if (strcmp(optarg, facility_list[i].name) == 0) {
					facility = facility_list[i].code;
					goto found;
				}
			}
			fprintf(stderr, "Facility not found\n");
			return -1;
		found:;
			break;
		default:
			break;
		}
	}
	openlog("mcklogd", LOG_PID, facility);

	if (flag == FLAG_KILL) {
		syslog(LOG_ERR, "mcklogd not support -k option.\n");
		return 0;
	}
	if (flag == FLAG_DAEMONIZE) {
		if (daemon(nochdir, noclose) == -1) {
			syslog(LOG_ERR, "failed to launch mcklogd.\n");
			return -1;
		}
	}

	envptr = getenv("SBINDIR");
	sprintf(command, "%s/ihkosctl 0 kmsg", envptr);
	while (1) {
		fp = popen(command, "r");
		if (fp) {
			while (fgets(buf, KPRINTF_LOCAL_BUF_LEN, fp)) {;
				if (buf[0] != '\n') {
					syslog(LOG_INFO, buf);
				}
			}
			pclose(fp);
		}
		sleep(interval);
	}
	return 0;
}
