#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <string.h>
#include <ihklib.h>
#include <errno.h>
#include <mckernel/ihklib_rusage.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include "util.h"

#define DEBUG

int main(int argc, char **argv)
{
	int ret;
	int i;
	int evfd;
	int epfd;
	struct epoll_event event;
	struct epoll_event events[1];
	FILE *fp;

	if (geteuid() != 0) {
		printf("Execute as a root\n");
	}

	evfd = ihk_os_get_eventfd(0, IHK_OS_EVENTFD_TYPE_STATUS);
	CHKANDJUMP(evfd < 0, 255, "geteventfd failed\n");

	epfd = epoll_create(1);
	CHKANDJUMP(epfd == -1, 255, "epoll_create failed\n");

	memset(&event, 0, sizeof(struct epoll_event));
	event.events = EPOLLIN;
	event.data.fd = evfd;
	ret = epoll_ctl(epfd, EPOLL_CTL_ADD, evfd, &event);
	CHKANDJUMP(ret != 0, 255, "epoll_ctl failed\n");

	daemon(1, 1);

	fp = fopen("./ihklib005.tmp", "w");
	CHKANDJUMP(fp == NULL, 255, "fopen failed\n");
	do {
		int nfd = epoll_wait(epfd, events, 1, -1);

		printf("%d events detected\n", nfd);
		for (i = 0; i < nfd; i++) {
			if (events[i].data.fd == evfd) {
				uint64_t counter;
				ssize_t nread;

				nread = read(evfd, &counter, sizeof(counter));
				CHKANDJUMP(nread == 0, -1, "EOF detected\n");
				CHKANDJUMP(nread == -1, -1,
					   "error: %s\n", strerror(errno));
				printf("counter=%ld\n", counter);
				fprintf(fp, "[OK] ihk_os_get_eventfd STATUS\n");
				goto found;
			}
		}
	} while (1);

 found:
	ret = 0;

 fn_exit:
	if (fp) {
		fclose(fp);
	}
	return ret;
 fn_fail:
	goto fn_exit;
}
