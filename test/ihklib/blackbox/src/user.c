#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/epoll.h>
#include "util.h"
#include "okng.h"
#include "user.h"

int __user_fork_exec(char *cmd, pid_t *pid)
{
	int ret;
//	char *argv[2] = { 0 };

	ret = fork();
	if (ret == 0) {
		ret = system(cmd);
		if (WIFEXITED(ret)) {
			int status = WEXITSTATUS(ret);

			INFO("%s: child exited with status of %d\n",
			     __func__, status);
			exit(status);
		}
		if (WIFSIGNALED(ret)) {
			int signum = WTERMSIG(ret);

			INFO("%s: child killed by %d\n",
			     __func__, signum);
			exit(signum);
		}
#if 0
		argv[0] = cmd;
		ret = execve(argv[0], argv, environ);
		if (ret) {
			printf("%s:%s execve: errno: %d, cmd: %s\n",
			       __FILE__, __LINE__, errno, cmd);
			exit(errno);
		}
#endif
	}

	*pid = ret;

	return ret;
}

int _user_fork_exec(char *filename, pid_t *pid, char *opt)
{
	char cmd[4096];

	sprintf(cmd, "%s/bin/mcexec %s %s/bin/%s",
		QUOTE(WITH_MCK),
		opt,
		QUOTE(CMAKE_INSTALL_PREFIX),
		filename);

	return __user_fork_exec(cmd, pid);
}

int user_fork_exec(char *filename, pid_t *pid)
{
	return _user_fork_exec(filename, pid, "");
}

int user_wait(pid_t *pid)
{
	int ret;
	int i;
	int wstatus;

	for (i = 0; i < 2; i++) {
		ret = waitpid(*pid, &wstatus, WNOHANG);
		if (ret > 0) {
			if (ret != *pid) {
				printf("%s:%d waitpid returned %d\n",
				       __FILE__, __LINE__, ret);
				ret = -EINVAL;
				goto out;
			}
			INFO("process with pid %d exited with status %d\n",
			     *pid, WEXITSTATUS(wstatus));
			*pid = -1;
			ret = 0;
			goto out;
		}

		if (ret == 0) {
			kill(*pid, 9);
			continue;
		}

		if (ret < 0) {
			int errno_save = errno;

			printf("%s:%d waitpid: errno: %d\n",
			       __FILE__, __LINE__, errno_save);
			ret = -errno_save;
			goto out;
		}
	}
 out:
	return ret;
}

int user_poll_fifo(int fd_fifo, int max_count)
{
	int ret;
	int fd_poll = -1;
	struct epoll_event event = { 0 };
	struct epoll_event events[1];
	int nfd;
	int i;
	int count = 0;
	int *messages;

	messages = calloc(max_count, sizeof(int));
	if (messages == NULL) {
		printf("%s: calloc failed\n", __func__);
		ret = -ENOMEM;
		goto out;
	}

	fd_poll = epoll_create1(0);
	INTERR(fd_poll == -1, "epoll_create returned %d\n", errno);

	event.data.fd = fd_fifo;
	event.events = EPOLLIN;

	ret = epoll_ctl(fd_poll, EPOLL_CTL_ADD, fd_fifo, &event);
	INTERR(ret, "epoll_ctl returned %d\n", errno);

 redo:
	nfd = epoll_wait(fd_poll, events, 1,
			 1000 * max_count * 0.3);

	if (nfd < 0) {
		int errno_save = errno;

		if (errno == EINTR) {
			goto redo;
		}

		printf("%s: epoll_wait returned %d\n",
		       __func__, errno_save);
		ret = -errno_save;
		goto out;
	}

	if (nfd == 0) {
		INFO("%s: epoll_wait timeout\n", __func__);

		ret = -ETIME;
		goto out;
	}

	for (i = 0; i < nfd; i++) {
		if (events[i].data.fd == fd_fifo) {
			ret = read(events[i].data.fd, messages,
				   sizeof(int) * max_count);

			if (ret == -1) {
				int errno_save = errno;

				printf("%s: read returned %d\n",
				       __func__, errno_save);
				ret = -errno_save;
				goto out;
			}

			if (ret == 0) {
				printf("%s: EOF detected\n",
				       __func__);
				goto out;
			}

			count += ret / sizeof(int);
		}
	}

	ret = count;
 out:
	if (fd_poll != -1) {
		close(fd_poll);
	}

	//INFO("%s: count: %d, ret %d\n", __func__, count, ret);
	return ret;
}
