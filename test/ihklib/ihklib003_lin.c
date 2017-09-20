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

#define DEBUG

#ifdef DEBUG
#define dprintf(...)                                            \
    do {                                                        \
	char msg[1024];                                         \
	sprintf(msg, __VA_ARGS__);                              \
	fprintf(stderr, "%s,%s", __FUNCTION__, msg);            \
    } while (0);
#define eprintf(...)                                            \
    do {                                                        \
	char msg[1024];                                         \
	sprintf(msg, __VA_ARGS__);                              \
	fprintf(stderr, "%s,%s", __FUNCTION__, msg);            \
    } while (0);
#else
#define dprintf(...) do {  } while (0)
#define eprintf(...) do {  } while (0)
#endif

#define CHKANDJUMP(cond, err, ...)                                      \
    do {                                                                \
		if(cond) {                                                      \
			eprintf(__VA_ARGS__);                                       \
			ret = err;                                                  \
			goto fn_fail;                                               \
		}                                                               \
    } while(0)


#define OKNG(cond, ...)													\
    do {                                                                \
		if(cond) {                                                      \
			printf("[OK] ");											\
			printf(__VA_ARGS__);										\
		} else {														\
            printf("[NG] ");											\
			printf(__VA_ARGS__);										\
			goto fn_fail;												\
		}																\
    } while(0)

#define PREFIX "/home/takagi/project/os/install"


int main(int argc, char** argv) {
    int ret = 0, ret_ihklib, ret_glibc;
	int i;
	int evfd;
    int epfd;
    struct epoll_event event;
    struct epoll_event events[1];
    int num;
	int err;

	if(geteuid() != 0) {
		printf("Execute as a root like: sudo bash -c 'LD_LIBRARY_PATH=/home/takagi/project/os/install/lib/ %s'", argv[0]);
	}	

	evfd = ihk_os_get_eventfd(0, IHK_OS_EVENTFD_TYPE_OOM);
	CHKANDJUMP(evfd < 0, 255, "geteventfd failed\n");

    epfd = epoll_create(1);
    CHKANDJUMP(epfd == -1, 255, "epoll_create failed\n");
	
	memset(&event, 0, sizeof(struct epoll_event));
	event.events = EPOLLIN;
	event.data.fd = evfd;
	ret_glibc = epoll_ctl(epfd, EPOLL_CTL_ADD, evfd, &event);
	CHKANDJUMP(ret_glibc != 0, 255, "epoll_ctl failed\n");

	daemon(1, 1);

	FILE* fp = fopen("./ihklib003.tmp", "w");
	CHKANDJUMP(fp == NULL, 255, "fopen failed\n");
    do {
        int nfd = epoll_wait(epfd, events, 1, -1);
        printf("%d events detected\n", nfd);
        for (i = 0; i < nfd; i++) {
			if (events[i].data.fd == evfd) {
				uint64_t counter;
				ssize_t nread = read(evfd, &counter, sizeof(counter));
				if(nread == 0) {
					printf("EOF detected\n");
					goto fn_exit;
				}
				if(nread == -1) {
					printf("error: %s\n", strerror(errno));
				} else {
					printf("counter=%ld\n", counter);
					fprintf(fp, "[OK] ihk_os_get_eventfd OOM\n");
					goto fn_exit;
				}
			}
        }
    } while (1);

 fn_exit:
	if (fp) {
		fclose(fp);
	}
    return ret;
 fn_fail:
    goto fn_exit;
}
