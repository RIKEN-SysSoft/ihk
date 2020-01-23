#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <ihklib.h>
#include <ihk/ihklib_private.h>
#include "util.h"
#include "okng.h"
#include "cpu.h"
#include "mem.h"
#include "os.h"
#include "user.h"
#include "params.h"
#include "linux.h"

const char param[] = "event type";
const char *values[] = {
	"out of memory",
	"panic",
	"hungup"
};

static pid_t watch_event(int i)
{
	int ret = 0;
	pid_t pid;
	int fd_poll = -1;
	int fd_event = -1;
	struct epoll_event event = { 0 };
	struct epoll_event events[1];
	int nfd;

	int target_eventfd_type[] = {
		IHK_OS_EVENTFD_TYPE_OOM,
		IHK_OS_EVENTFD_TYPE_STATUS,
		IHK_OS_EVENTFD_TYPE_STATUS
	};

	pid = fork();
	if (pid) {
		return pid;
	}

	fd_poll = epoll_create1(0);
	INTERR(fd_poll == -1, "epoll_create returned %d\n", errno);

	fd_event = ihk_os_get_eventfd(0, target_eventfd_type[i]);
	INTERR(fd_event < 0, "ihk_os_get_eventfd returned %d", ret);

	event.data.fd = fd_event;
	event.events = EPOLLIN;

	ret = epoll_ctl(fd_poll, EPOLL_CTL_ADD, fd_event, &event);
	INTERR(ret, "epoll_ctl returned %d\n", errno);

	/* wait for ten sec */
	nfd = epoll_wait(fd_poll, events, 1, 10000);

	if (nfd < 0 && errno == EINTR) {
		int errno_save = errno;

		INFO("epoll_wait timeout\n");
		ret = -errno_save;
		goto out;
	}

	INTERR(nfd < 0, "epoll_wait returned %d\n", errno);

	for (i = 0; i < nfd; i++) {
		if (events[i].data.fd == fd_event) {
			uint64_t counter;
			ssize_t ret;

			ret = read(events[i].data.fd, &counter,
				   sizeof(counter));

			INTERR(ret == -1, "read returned %d\n", errno);
			INTERR(ret == 0, "EOF detected\n");

			INFO("target event detected\n");

			ret = 0;
			goto out;
		}
	}

	ret = -EINVAL;
 out:
	if (fd_poll != -1) {
		close(fd_poll);
	}
	if (fd_event != -1) {
		close(fd_event);
	}
	exit(ret);
}

int main(int argc, char **argv)
{
	int ret;
	int i;
	pid_t pid = -1;

	params_getopt(argc, argv);

	enum ihklib_os_status target_status[] = {
		IHK_STATUS_RUNNING,
		IHK_STATUS_PANIC,
		IHK_STATUS_HUNGUP,
	};

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	ret = cpus_reserve();
	INTERR(ret, "cpus_reserve returned %d\n", ret);

	ret = mems_reserve();
	INTERR(ret, "mems_reserve returned %d\n", ret);

	int fd;

	/* Activate and check */
	for (i = 0; i < 3; i++) {
		int wstatus;
		uid_t pid_poll;
		char exit_status;

		START("test-case: %s: %s\n", param, values[i]);

		ret = ihk_create_os(0);
		INTERR(ret, "ihk_create_os returned %d\n", ret);

		ret = cpus_os_assign();
		INTERR(ret, "cpus_os_assign returned %d\n", ret);

		ret = mems_os_assign();
		INTERR(ret, "mems_os_assign returned %d\n", ret);

		ret = os_load();
		INTERR(ret, "os_load returned %d\n", ret);

		ret = os_kargs();
		INTERR(ret, "os_kargs returned %d\n", ret);

		ret = ihk_os_boot(0);
		INTERR(ret, "ihk_os_boot returned %d\n", ret);

		pid_poll = watch_event(i);

		usleep(100000);

		/* trigger event */
		switch (target_status[i]) {
		case IHK_STATUS_RUNNING:
			ret = user_fork_exec("oom", &pid);
			INTERR(ret < 0, "user_fork_exec returned %d\n", ret);
			break;
		case IHK_STATUS_PANIC:
			ret = user_fork_exec("panic", &pid);
			INTERR(ret < 0, "user_fork_exec returned %d\n", ret);
			break;
		case IHK_STATUS_HUNGUP:
			ret = user_fork_exec("hungup", &pid);
			INTERR(ret < 0, "user_fork_exec returned %d\n", ret);

			/* wait until McKernel start ihk_mc_delay_us() */
			usleep(0.25 * 1000000);

			fd = ihklib_os_open(0);
			INTERR(fd < 0, "ihklib_os_open returned %d\n", fd);

			ioctl(fd, IHK_OS_DETECT_HUNGUP);
			usleep(0.25 * 1000000);
			ioctl(fd, IHK_OS_DETECT_HUNGUP);

			close(fd);
			break;
		default:
			break;
		}

		/* wait until os status changes to the target status */
		ret = os_wait_for_status(target_status[i]);
		INTERR(ret, "os status didn't change to %d\n",
		       target_status[i]);

		INFO("wait for poll process\n");

		ret = waitpid(pid_poll, &wstatus, 0);
		INTERR(ret == -1, "waitpid returned %d\n", errno);

		exit_status = WEXITSTATUS(wstatus);
		OKNG(exit_status == 0,
		     "poll process detected target event, status: %d\n",
		     exit_status);

		/* Clean up */
		user_wait(&pid);
		linux_kill_mcexec();

		ret = ihk_os_shutdown(0);
		INTERR(ret, "ihk_os_shutdown returned %d\n", ret);

		ret = os_wait_for_status(IHK_STATUS_INACTIVE);
		INTERR(ret, "os status didn't change to %d\n",
		       IHK_STATUS_INACTIVE);

		ret = cpus_os_release();
		INTERR(ret, "cpus_os_release returned %d\n", ret);

		ret = mems_os_release();
		INTERR(ret, "mems_os_release returned %d\n", ret);

		ret = ihk_destroy_os(0, 0);
		INTERR(ret, "ihk_destroy_os returned %d\n", ret);
	}

	ret = 0;
 out:
	if (pid > 0) {
		user_wait(&pid);
		linux_kill_mcexec();
	}

	if (ihk_get_num_os_instances(0)) {
		ihk_os_shutdown(0);
		os_wait_for_status(IHK_STATUS_INACTIVE);
		cpus_os_release();
		mems_os_release();
		ihk_destroy_os(0, 0);
	}
	cpus_release();
	mems_release();
	linux_rmmod(1);

	return ret;
}

