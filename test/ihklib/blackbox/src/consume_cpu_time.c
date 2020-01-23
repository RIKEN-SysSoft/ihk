#define _GNU_SOURCE
#include <sched.h>
#include <sys/syscall.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <string.h>
#include "mckernel.h"

#define NS_PER_SEC	1000000000UL

struct percpu_time {
	int cpu_id;
	long time; /* nanoseconds */
};

static int user_cpu_time(long user_nsec)
{
	int ret;
	struct timespec tv_start = { 0 };
	struct timespec tv_end = { 0 };
	long time_start, time_end;

	ret = clock_gettime(CLOCK_MONOTONIC, &tv_start);
	if (ret == -1) {
		int errno_save = errno;

		printf("%s: clock_gettime failed %d\n",
			__FILE__, errno);
		ret = -errno_save;
		goto out;
	}
	time_start = tv_start.tv_sec * NS_PER_SEC +
			tv_start.tv_nsec;
	do {
		nop1000000;
		ret = clock_gettime(CLOCK_MONOTONIC, &tv_end);
		if (ret == -1) {
			int errno_save = errno;

			printf("%s: clock_gettime failed %d\n",
				__FILE__, errno);
			ret = -errno_save;
			goto out;
		}
		time_end = tv_end.tv_sec * NS_PER_SEC +
			tv_end.tv_nsec;
	} while ((time_end - time_start) < user_nsec);

	ret = 0;
out:
	return ret;
}

static int kernel_cpu_time(long kernel_nsec)
{
	struct timespec tv;
	int ret;

	tv.tv_sec = kernel_nsec / NS_PER_SEC;
	tv.tv_nsec = kernel_nsec % NS_PER_SEC;

	ret = syscall(2003, &tv);
	if (ret) {
		printf("%s: syscall 2003 returned %d\n",
			__FILE__, errno);
		ret = -errno;
		goto out;
	}

	ret = 0;
out:
	return ret;
}

static int parse_unit(char *args, int *cpu_id, double *sec)
{
	int ret;

	char *ps = strdup(args);
	if (!ps) {
		ret = -errno;
		printf("%s: strdup failed\n", __func__);
		goto out;
	}

	char *p = ps;

	while (*p != ':' && *p != '\0') {
		p++;
	}

	if (*p == ':') {
		*p = '\0';
	}
	else {
		ret = -EINVAL;
		printf("%s: invalid format (<cpu>:<time>)\n", __func__);
		goto out;
	}
	p++;

	*cpu_id = atoi(ps);
	*sec = strtod(p, NULL);

	ret = 0;
out:
	if (ps) {
		free(ps);
	}
	return ret;
}

static int parse_args(char *args, struct percpu_time **cpus)
{
	int ret, i = 0, count = 1;
	char *_args = NULL;
	char *c = NULL;
	char *tok;

	_args = strdup(args);
	if (!_args) {
		int errno_save = errno;

		printf("%s: strdup returned %d\n", __func__, errno);
		ret = -errno_save;
		goto out;
	}

	c = _args;
	while (*c != '\0') {
		if (*c == ',' && *(c + 1) != '\0') {
			count++;
		}
		c++;
	}

	*cpus = malloc(sizeof(struct percpu_time) * count);
	if (!(*cpus)) {
		int errno_save = errno;

		printf("%s: malloc returned %d\n", __func__, errno);
		ret = -errno_save;
		goto out;
	}

	tok = strtok(_args, ",");
	while (tok) {
		int cpu_temp = 0;
		double sec_temp = 0.0;
		long time_temp = 0;

		ret = parse_unit(tok, &cpu_temp, &sec_temp);
		if (ret) {
			printf("%s: parse unit returned %d\n", __func__, ret);
			goto out;
		}
		printf("cpu: %d  sec: %f\n", cpu_temp, sec_temp);

		time_temp = sec_temp * NS_PER_SEC;

		(*cpus)[i].cpu_id = cpu_temp;
		(*cpus)[i++].time = time_temp;
		tok = strtok(NULL, ",");
	}

	ret = count;
out:
	free(_args);
	return ret;
}

int main(int argc, char **argv)
{
	int ret;
	int opt;
	int message =  1;
	int fd_in = -1, fd_out = -1;
	int user_mode = 0, kernel_mode = 0;
	long time_user, time_kernel;
	int cpu_count = 0;
	struct percpu_time *cpus = NULL;

	fd_in = open(argv[1], O_RDWR);
	if (fd_in == -1) {
		int errno_save = errno;

		printf("%s: open returned %d\n", __FILE__, errno);
		ret = -errno_save;
		goto out;
	}

	fd_out = open(argv[2], O_RDWR);
	if (fd_out == -1) {
		int errno_save = errno;

		printf("%s: open returned %d\n", __FILE__, errno);
		ret = -errno_save;
		goto out;
	}

	while ((opt = getopt(argc, argv, "u:k:c:")) != -1) {
		switch (opt) {
		case 'u': /* user mode */
			user_mode = 1;
			time_user = strtod(optarg, NULL) * NS_PER_SEC;
			break;
		case 'k': /* kernel mode */
			kernel_mode = 1;
			time_kernel = strtod(optarg, NULL) * NS_PER_SEC;
			break;
		case 'c': /* run on specific CPU(s) */
			ret = parse_args(optarg, &cpus);
			if (ret <= 0) {
				printf("%s: parse args failed with %d\n",
					__FILE__, ret);
				goto out;
			}
			cpu_count = ret;
			break;
		default:
			printf("unknown option %c\n", optopt);
			exit(1);
		}
	}

	/* Let parent take stat */
	ret = write(fd_out, &message, sizeof(int));
	if (ret != sizeof(int)) {
		int errno_save = errno;

		printf("%s: write returned %d, errno: %d\n",
		       __FILE__, ret, errno);
		ret = ret >= 0 ? ret : -errno_save;
		goto sync_out;
	}

	/* Wait until parent takes reference stat */
	ret = read(fd_in, &message, sizeof(int));
	if (ret != sizeof(int)) {
		int errno_save = errno;

		printf("%s: read returned %d, errno: %d\n",
		       __FILE__, ret, errno);
		ret = ret >= 0 ? ret : -errno_save;
		goto sync_out;
	}

	if (user_mode) {
		ret = user_cpu_time(time_user);
		if (ret) {
			printf("%s: user_cpu_time returned %d\n",
				__FILE__, ret);
			goto sync_out;
		}
	}

	if (kernel_mode) {
		ret = kernel_cpu_time(time_kernel);
		if (ret) {
			printf("%s: kernel_cpu_time returned %d\n",
				__FILE__, ret);
			goto sync_out;
		}
	}

	if (cpus) {
		int iter;
		cpu_set_t cpuset;

		CPU_ZERO(&cpuset);

		for (iter = 0; iter < cpu_count; iter++) {
			CPU_SET(cpus[iter].cpu_id, &cpuset);

			ret = sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);
			if (ret) {
				int errno_save = -errno;

				printf("%s: sched_setaffinity returned %d\n",
					__FILE__, ret);
				ret = -errno_save;
				goto sync_out;
			}

			ret = user_cpu_time(cpus[iter].time);
			if (ret) {
				printf("%s: user_cpu_time returned %d\n",
					__FILE__, ret);
				goto sync_out;
			}

			CPU_CLR(cpus[iter].cpu_id, &cpuset);
		}
	}

sync_out:
	/* Let parent take stat */
	ret = write(fd_out, &message, sizeof(int));
	if (ret != sizeof(int)) {
		int errno_save = errno;

		printf("%s: write returned %d, errno: %d\n",
		       __FILE__, ret, errno);
		ret = ret >= 0 ? ret : -errno_save;
		goto out;
	}

	/* Wait until parent takes usage stat */
	ret = read(fd_in, &message, sizeof(int));
	if (ret != sizeof(int)) {
		int errno_save = errno;

		printf("%s: read returned %d, errno: %d\n",
		       __FILE__, ret, errno);
		ret = ret >= 0 ? ret : -errno_save;
		goto out;
	}

	ret = 0;

out:
	if (cpus) {
		free(cpus);
	}
	if (fd_in != -1) {
		close(fd_in);
	}
	if (fd_out != -1) {
		close(fd_out);
	}
	return ret;
}
