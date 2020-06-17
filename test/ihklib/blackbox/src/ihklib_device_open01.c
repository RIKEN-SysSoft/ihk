#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>
#include <sched.h>
#include <ihklib.h>
#include <stdlib.h>
#include <unistd.h>
#include <ihk/ihklib_private.h>
#include "util.h"
#include "okng.h"
#include "cpu.h"
#include "mem.h"
#include "os.h"
#include "params.h"
#include "linux.h"

const char param[] = "timing of opening /dev/mcd0";
const char *values[] = {
	"while assigining / releasing resources",
};
#define DURATION_SLEEP 500000

int main(int argc, char **argv)
{
	int ret;
        key_t key = ftok(argv[0], 10);
        int *shm;
        int shmid;
        struct shmid_ds shmctl_buf;
	pid_t pid = getpid();
	int status;
	int fd = -1;

	params_getopt(argc, argv);

        shmid = shmget(key, sizeof(int), IPC_CREAT | 0660);
        INTERR(shmid == -1, "shmget failed: %s\n",
	       strerror(errno));

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	/* Activate and check */
	START("test-case: %s: %s\n", param, values[0]);

	pid = fork();
	INTERR(pid == -1, "fork returned %d\n", errno);

	if (pid == 0) {
                shm = shmat(shmid, NULL, 0);
                INTERR(shm == (void *)-1,
		       "shmat failed: %s\n", strerror(errno));

		/* post */
		*shm = 1;

		while (*shm != 2) {
			struct ihk_device_get_kmsg_buf_desc desc_get =
				{ .os_index = 0 };
			char kmsg[IHK_KMSG_SIZE] = { 0 };
			struct ihk_device_read_kmsg_buf_desc desc_read =
				{ .shift = 0, .buf = kmsg };

			fd = ihklib_device_open(0); 
			INTERR(fd < 0, "ihklib_device_open returned %d\n", fd);
	
			ret = ioctl(fd, IHK_DEVICE_GET_KMSG_BUF, &desc_get);
			ret = ret == 0 ? ret : -errno;
			INFO("child: IHK_DEVIE_GET_KMSG_BUF: %s\n",
			     ret == 0 ? "succeeded" : strerror(-ret));
			if (ret) {
				if (-ret == ENOENT) {
					usleep(DURATION_SLEEP);
					close(fd);
					fd = -1;
					continue;
				}
				INTERR(1, "IHK_DEVIE_GET_KMSG_BUF "
				       "returned %d\n", -ret);
			}

			/* let open-close duration overwrap with parent's */
			usleep(DURATION_SLEEP);
			close(fd);
			fd = -1;

			desc_read.handle = desc_get.handle;
	
			fd = ihklib_device_open(0);
			INTERR(fd < 0, "ihklib_device_open returned %d\n", -errno);

			ret = ioctl(fd, IHK_DEVICE_READ_KMSG_BUF, (unsigned long)&desc_read);
			INTERR(ret < 0 || ret > IHK_KMSG_SIZE,
				   "IHK_DEVICE_READ_KMSG_BUF returned %d\n", ret);
			usleep(DURATION_SLEEP);
			close(fd);
			fd = -1;

			INFO("child: kmsg:\n%s\n", kmsg);

			fd = ihklib_device_open(0); 
			INTERR(fd < 0, "ihklib_device_open returned %d\n", fd);
			ret = ioctl(fd, IHK_DEVICE_RELEASE_KMSG_BUF, desc_get.handle);
			INTERR(ret != 0, "IHK_DEVICE_RELEASE_KMSG_BUF failed\n");
			usleep(DURATION_SLEEP);
			close(fd);
			fd = -1;
		}

                ret = shmdt(shm);
                INTERR(ret == -1, "shmdt failed\n");

		exit(0);
	} else {
		int i;

                shm = shmat(shmid, NULL, 0);
                INTERR(shm == (void *)-1,
		       "shmat failed: %s\n", strerror(errno));

		/* wait */
                INFO("child: wait until posted\n");
                while ((*shm) == 0) {
                        sched_yield();
                };

		for (i = 0; i < 10; i++) {
			char kmsg[IHK_KMSG_SIZE] = { 0 };
			char *keyword = NULL;

			ret = cpus_reserve();
			INTERR(ret, "cpus_reserve returned %d\n", ret);

			ret = mems_reserve();
			INTERR(ret, "mems_reserve returned %d\n", ret);

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

			ret = ihk_os_kmsg(0, kmsg, IHK_KMSG_SIZE);
			INTERR(ret < 0, "ihk_os_kmsg returned %d\n", ret);
		
			keyword = strstr(kmsg, "booted");
			INTERR(keyword == NULL, "\"booted\" not found in kmsg\n");

			os_wait_for_status(IHK_STATUS_RUNNING);
			ret = ihk_os_get_status(0);
			INTERR(ret != IHK_STATUS_RUNNING,
			       "os status (%d) didn't change to RUNNING\n", ret);

			ret = ihk_os_shutdown(0);
			INTERR(ret, "ihk_os_shutdown returned %d\n", ret);

			os_wait_for_status(IHK_STATUS_INACTIVE);
			ret = ihk_os_get_status(0);
			INTERR(ret != IHK_STATUS_INACTIVE,
			       "os status (%d) didn't change to INACTIVE\n", ret);

			ret = cpus_os_release();
			INTERR(ret, "cpus_os_release returned %d\n", ret);

			ret = mems_os_release();
			INTERR(ret, "mems_os_release returned %d\n", ret);

			ret = ihk_destroy_os(0, 0);
			INTERR(ret, "ihk_destroy_os returned %d\n", ret);

			ret = cpus_release();
			INTERR(ret, "cpus_release returned %d\n", ret);

			ret = mems_release();
			INTERR(ret, "mems_release returned %d\n", ret);
		}

		/* notify */
		*shm = 2;

		ret = waitpid(pid, &status, 0);
                INTERR(ret == -1, "waitpid failed\n");

		OKNG(WEXITSTATUS(status) == 0,
		     "child exited without errors\n");

                ret = shmctl(shmid, IPC_RMID, &shmctl_buf);
                INTERR(ret == -1, "shmctl failed\n");

                ret = shmdt(shm);
                INTERR(ret == -1, "shmdt failed\n");
	}

	ret = 0;
 out:
	if (pid == 0) {
		if (fd != -1) {
			close(fd);
		}
		exit(1);
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
