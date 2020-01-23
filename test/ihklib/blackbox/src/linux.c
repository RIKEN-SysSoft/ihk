#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include "util.h"
#include "okng.h"
#include "linux.h"
#include <sys/stat.h>
#include <ihklib.h>

static int linux_lsmod(char *_fn)
{
	int count = 0;
	int ret;
	FILE *st = NULL;
	char cmd[1024];
	char *name, *ext, *delim;
	char *fn = NULL;

	fn = strdup(_fn);
	name = strrchr(fn, '/');
	if (name) {
		name++;
	} else {
		name = fn;
	}

	ext = strrchr(name, '.');
	if (ext) {
		ext[0] = 0;
	}

	while ((delim = strchr(name, '-'))) {
		delim[0] = '_';
	}

	sprintf(cmd, "lsmod | cut -d' ' -f1 | grep -c -x %s", name);

	if ((st = popen(cmd, "r")) == NULL) {
		int errno_save = errno;

		dprintf("%s: error: popen returned %d\n",
			__func__, errno_save);
		ret = -errno_save;
		goto out;
	}

	ret = fscanf(st, "%d\n", &count);

	if (ret == 0) {
		dprintf("%s: error: fscanf returned zero\n",
			__func__);
		ret = -EINVAL;
		goto out;
	}

	if (ret == -1) {
		int errno_save = errno;

		dprintf("%s: error: fscanf returned %d\n",
			__func__, errno_save);
		ret = -errno_save;
		goto out;
	}

	ret = count;
 out:
	if (st) {
		pclose(st);
	}

	free(fn);

	return ret;
}

int _linux_insmod(char *fn, char *opts)
{
	int ret;
	char cmd[1024];

	ret = linux_lsmod(fn);
	if (ret < 0) {
		printf("%s: error: linux_lsmod %s returned %d\n",
		       __func__, fn, ret);
		goto out;
	} else if (ret > 0) {
		INFO("%s: warning: %s is already loaded\n",
		     __func__, fn);
		ret = 0;
		goto out;
	}

	sprintf(cmd, "insmod %s ", fn);
	if (opts) {
		strcat(cmd, opts);
	}
	ret = system(cmd);
	ret = WEXITSTATUS(ret);
	if (ret) {
		printf("%s: error: %s returned %d\n",
		       __func__, cmd, ret);
		goto out;
	}
	ret = 0;
 out:
	return ret;
}

int linux_insmod(int verbose)
{
	int ret;
	char fn[1024];

	sprintf(fn, "%s/kmod/ihk.ko", QUOTE(WITH_MCK));
	ret = _linux_insmod(fn, NULL);
	if (ret) {
		printf("%s: error: _linux_insmod %s returned %d\n",
		       __func__, fn, ret);
		goto out;
	}

	sprintf(fn, "%s/kmod/ihk-%s.ko",
		QUOTE(WITH_MCK), QUOTE(BUILD_TARGET));
	ret = _linux_insmod(fn, "ihk_ikc_irq_core=0");
	if (ret) {
		printf("%s: error: _linux_insmod %s returned %d\n",
		       __func__, fn, ret);
		goto out;
	}

	sprintf(fn, "%s/kmod/mcctrl.ko", QUOTE(WITH_MCK));
	ret = _linux_insmod(fn, NULL);
	if (ret) {
		printf("%s: error: _linux_insmod %s returned %d\n",
		       __func__, fn, ret);
		goto out;
	}

	ret = 0;
out:
	return ret;
}

int linux_chmod(int dev_index)
{
	int i, ret = 0;
	int num_os_instances;
	int *os_indices = NULL;

	ret = ihk_get_num_os_instances(dev_index);
	INTERR(ret < 0,
		"ihk_get_num_os_instances failed with errno %d\n", errno);
	num_os_instances = ret;

	os_indices = calloc(num_os_instances, sizeof(int));
	INTERR(!os_indices, "calloc failed with errno: %d\n", errno);

	ret = ihk_get_os_instances(dev_index, os_indices, num_os_instances);
	INTERR(ret, "ihk_get_os_instances failed with errno: %d\n", errno);

	for (i = 0; i < num_os_instances; i++) {
		char os_filename[4096];
		struct stat os_stat;

		sprintf(os_filename, "/dev/mcos%d", os_indices[i]);
		ret = stat(os_filename, &os_stat);
		INTERR(ret, "stat failed with errno; %d\n", errno);

		ret = chmod(os_filename, 0666);
		INTERR(ret, "chmod failed with errno; %d\n", errno);
	}

	ret = 0;
out:
	return ret;
}

int linux_wait_chmod(int dev_index)
{
	int ret;
	int i;
	char fn[4096];
	struct stat os_stat;

	sprintf(fn, "/dev/mcos0");
	ret = stat(fn, &os_stat);
	INTERR(ret, "stat failed with errno; %d\n", errno);

	for (i = 0; i < 60; i++) {
		if ((os_stat.st_mode & 0777) == 0666) {
			ret = 0;
			goto out;
		}

		usleep(1000000);

		ret = stat(fn, &os_stat);
		INTERR(ret, "stat failed with errno; %d\n", errno);
	}

	system("ls -l /dev/mcos0");

	ret = -ETIME;
	goto out;

out:
	return ret;
}

int _linux_rmmod(char *fn)
{
	int ret;
	char cmd[1024];

	ret = linux_lsmod(fn);
	if (ret < 0) {
		printf("%s: error: linux_lsmod %s returned %d\n",
		       __func__, fn, ret);
		goto out;
	} else if (ret == 0) {
		INFO("%s: warning: %s is not loaded\n",
		     __func__, fn);
		ret = 0;
		goto out;
	}

	sprintf(cmd, "rmmod %s", fn);
	ret = system(cmd);
	ret = WEXITSTATUS(ret);
	INTERR(ret, "%s returned %d\n", cmd, ret);

	ret = 0;
 out:
	return ret;
}

int linux_rmmod(int verbose)
{
	int ret;
	char fn[1024];

	ret = ihk_get_num_os_instances(0);
	if (ret) {
		printf("%s: error: os instance found\n",
		       __func__);
		goto out;
	}

	sprintf(fn, "%s/kmod/mcctrl.ko", QUOTE(WITH_MCK));
	ret = _linux_rmmod(fn);
	if (ret) {
		printf("%s: error: _linux_rmmod %s returned %d\n",
		       __func__, fn, ret);
		goto out;
	}

	sprintf(fn, "%s/kmod/ihk-%s.ko",
		QUOTE(WITH_MCK), QUOTE(BUILD_TARGET));
	ret = _linux_rmmod(fn);
	if (ret) {
		printf("%s: error: _linux_rmmod %s returned %d\n",
		       __func__, fn, ret);
		goto out;
	}

	sprintf(fn, "%s/kmod/ihk.ko", QUOTE(WITH_MCK));
	ret = _linux_rmmod(fn);
	if (ret) {
		printf("%s: error: _linux_rmmod %s returned %d\n",
		       __func__, fn, ret);
		goto out;
	}

	ret = 0;
out:
	return ret;
}

int linux_kill_mcexec(void)
{
	int ret;
	char cmd[1024];
	int pid;
	FILE *fp = NULL;
	int killing = 0;

	while (1) {
		sprintf(cmd, "pidof mcexec | awk '{ print $1 }'");

		if ((fp = popen(cmd, "r")) == NULL) {
			int errno_save = errno;

			dprintf("%s: error: popen returned %d\n",
				__func__, errno_save);
			ret = -errno_save;
			goto out;
		}

		ret = fscanf(fp, "%d\n", &pid);
		if (ret == EOF || ret == 0) {
			ret = 0;
			goto out;
		}

		if (killing) {
			goto next;
		}

		if (ret == 1) {
			INFO("killing %d...\n", pid);
			ret = kill(pid, 9);
			if (ret) {
				int errno_save = errno;

				dprintf("%s: error: kill returned %d\n",
					__func__, errno_save);
				ret = -errno_save;
				goto out;
			}
			killing = 1;
		}
 next:
		pclose(fp);
		fp = NULL;
	}
 out:
	if (fp) {
		pclose(fp);
	}
	return ret;
}
