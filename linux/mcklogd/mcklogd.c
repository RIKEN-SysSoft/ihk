#include <stdio.h>
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
	char pid_file_path[] = "./mcklogd.pid";
	char buf[KPRINTF_LOCAL_BUF_LEN];
	char command[512];
	char *envptr;
	FILE *pid_file;
	FILE *fp;
	pid_t pid;

	/**
	 * mcklogd[PID]: log message
	 * /var/log/local6
	 */
	openlog("mcklogd", LOG_PID, LOG_LOCAL6);

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
