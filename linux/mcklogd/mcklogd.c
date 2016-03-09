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
	int count = 0;
	int interval = 1;
	int buffer_wait = 0;
	int facility = LOG_LOCAL6;
	int i;
	int len;
	char pid_file_path[] = "./mcklogd.pid";
	char buf[512];
	char command[512];
	char inifile[512];
	char item[256];
	char value[256];
	char *envptr;
	char *delimiter;
	FILE *pid_file;
	FILE *fp;
	pid_t pid;
	struct {
		char name[12];
		int code;
	} facility_list[8] = {
		"LOG_LOCAL0", LOG_LOCAL0,
		"LOG_LOCAL1", LOG_LOCAL1,
		"LOG_LOCAL2", LOG_LOCAL2,
		"LOG_LOCAL3", LOG_LOCAL3,
		"LOG_LOCAL4", LOG_LOCAL4,
		"LOG_LOCAL5", LOG_LOCAL5,
		"LOG_LOCAL6", LOG_LOCAL6,
		"LOG_LOCAL7", LOG_LOCAL7
	};

	/**
	 * mcklogd[PID]: log message
	 * default facility: LOG_LOCAL6 (/var/log/local6)
	 */
	strcpy(inifile, "./mcklogd.ini");
	fp = fopen(inifile, "r");
	if (fp != NULL) {
		while(fgets(buf, sizeof(buf), fp) != NULL) {
printf("buf = %s\n", buf);
			if (buf[0] == '#') continue;
			len = strlen(buf);
			if (len > 0) {
				if (buf[len-1] == '\n') {
					buf[len-1] = '\0';
				}
			}
			delimiter = strstr(buf, "=");
printf("deliter = %p\n", delimiter);
			if (delimiter == NULL) continue;
			*delimiter = '\0';
			sscanf(buf, "%s", item);
			sscanf(delimiter+1, "%s", value);
printf("item = %s value = %s\n", item, value);
			if (strcmp(item, "FACILITY") == 0) {
				for (i = 0; i < 8; i++) {
					if (strcmp(value, facility_list[i].name) == 0) {
						facility = facility_list[i].code;
						break;
					}
				}
			}
		}
	}
	openlog("mcklogd", LOG_PID, facility);

	const struct option longopt[] = {
		{"help", no_argument, NULL, '?'},
		{"daemonize", no_argument, NULL, 'd'},
		{"kill", no_argument, NULL, 'k'},
		{"interval", required_argument, NULL, 'i'},
		{NULL, 0, NULL, 0}
	};

	flag = FLAG_DAEMONIZE;
	
	while ((opt = getopt_long(argc, argv, "dki:", longopt, NULL)) != -1) {
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
		default:
			break;
		}
	}

	if (flag == FLAG_KILL) {
		pid_file = fopen(pid_file_path, "r");
		if (pid_file != NULL) {
			fscanf(pid_file, "%d\n", &pid);
			fclose(pid_file);
		} else {
			syslog(LOG_ERR, "failed to open pidfile.\n");
			return -1;
		}
		if (pid) {
			if (kill(pid, SIGKILL) == 0) {
				syslog(LOG_INFO, "mcklogd stopped.\n");
			} else {
				syslog(LOG_ERR, "no mcklogd started.\n");
				return -1;
			}
			return 0;
		}
	}
	if (flag == FLAG_DAEMONIZE) {
		if (daemon(nochdir, noclose) == -1) {
			syslog(LOG_ERR, "failed to launch mcklogd.\n");
			return -1;
		}
	}

	pid = getpid();
	pid_file = fopen(pid_file_path, "w+");
	if (pid_file != NULL) {
		fprintf(pid_file, "%d\n", pid);
		fclose(pid_file);
	} else {
		syslog(LOG_ERR, "failed to record process id to file.\n");
		return -1;
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
