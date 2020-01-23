#define _GNU_SOURCE
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <string.h>

pthread_barrier_t barrier;

static void *thread_func(void *arg)
{
	pthread_barrier_wait(&barrier);

	return NULL;
}

int main(int argc, char **argv)
{
	int i, ret;
	int opt;
	int message =  1;
	int fd_in = -1, fd_out = -1;
	int num_threads = 1;

	pthread_t *threads = NULL;

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

	while ((opt = getopt(argc, argv, "n:")) != -1) {
		switch (opt) {
		case 'n':
			num_threads = atoi(optarg);
			break;
		default:
			printf("unknown option %c\n", optopt);
			exit(1);
		}
	}

	threads = malloc(sizeof(pthread_t) * num_threads);
	if (!threads) {
		ret = -errno;
		printf("%s: malloc failed with error %d\n", __FILE__, ret);
		goto out;
	}

	ret = pthread_barrier_init(&barrier, NULL, num_threads + 1);
	if (ret) {
		int errno_save = -errno;

		printf("%s: pthread_barrier_init returned %d\n",
		       __FILE__, errno_save);
		ret = -errno_save;
		goto out;
	}

	/* Let parent take stat */
	ret = write(fd_out, &message, sizeof(int));
	if (ret != sizeof(int)) {
		int errno_save = errno;

		printf("%s: write returned %d, errno: %d\n",
		       __FILE__, ret, errno);
		ret = ret >= 0 ? ret : -errno_save;
		goto out;
	}

	/* Wait until parent takes reference stat */
	ret = read(fd_in, &message, sizeof(int));
	if (ret != sizeof(int)) {
		int errno_save = errno;

		printf("%s: read returned %d, errno: %d\n",
		       __FILE__, ret, errno);
		ret = ret >= 0 ? ret : -errno_save;
		goto out;
	}

	ret = pthread_create(&threads[0], NULL, thread_func, NULL);
	if (ret) {
		printf("%s: pthread_create(0) returned %d\n", __FILE__, ret);
		goto out;
	}

	ret = write(fd_out, &message, sizeof(int));
	if (ret != sizeof(int)) {
		int errno_save = errno;

		printf("%s: write returned %d, errno: %d\n",
		       __FILE__, ret, errno);
		ret = ret >= 0 ? ret : -errno_save;
		goto out;
	}

	ret = read(fd_in, &message, sizeof(int));
	if (ret != sizeof(int)) {
		int errno_save = errno;

		printf("%s: read returned %d, errno: %d\n",
		       __FILE__, ret, errno);
		ret = ret >= 0 ? ret : -errno_save;
		goto out;
	}

	if (num_threads > 1) {
		for (i = 1; i < num_threads; i++) {
			ret = pthread_create(&threads[i], NULL,
					thread_func, NULL);
			if (ret) {
				printf("%s: pthread_create(%d) returned %d\n",
					__FILE__, i, ret);
				goto out;
			}
		}

		/* release parent to collect rusage */
		ret = write(fd_out, &message, sizeof(int));
		if (ret != sizeof(int)) {
			int errno_save = errno;

			printf("%s: write returned %d, errno: %d\n",
			       __FILE__, ret, errno);
			ret = ret >= 0 ? ret : -errno_save;
			goto out;
		}

		ret = read(fd_in, &message, sizeof(int));
		if (ret != sizeof(int)) {
			int errno_save = errno;

			printf("%s: read returned %d, errno: %d\n",
			       __FILE__, ret, errno);
			ret = ret >= 0 ? ret : -errno_save;
			goto out;
		}
	}

	pthread_barrier_wait(&barrier);

	for (i = 0; i < num_threads; i++) {
		ret = pthread_join(threads[i], NULL);
		if (ret) {
			printf("%s: pthread_join returned %d\n", __FILE__, ret);
			goto out;
		}
	}

	/* collect rusage data after pthread_join */
	ret = write(fd_out, &message, sizeof(int));
	if (ret != sizeof(int)) {
		int errno_save = errno;

		printf("%s: write returned %d, errno: %d\n",
		       __FILE__, ret, errno);
		ret = ret >= 0 ? ret : -errno_save;
		goto out;
	}

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
	pthread_barrier_destroy(&barrier);
	if (threads) {
		free(threads);
	}
	if (fd_in != -1) {
		close(fd_in);
	}
	if (fd_out != -1) {
		close(fd_out);
	}

	return ret;
}
