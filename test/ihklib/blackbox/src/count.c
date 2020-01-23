#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>

#define MAX_COUNT 10

int main(int argc, char **argv)
{
	int ret;
	int fd = -1;
	int message;
	int i;

	fd = open(argv[1], O_RDWR);
	if (fd == -1) {
		int errno_save = errno;

		printf("%s: open returned %d\n", __FILE__, errno);
		ret = -errno_save;
		goto out;
	}

	/* Wait until parent gets ready */
	ret = read(fd, &message, sizeof(int));
	if (ret == -1) {
		int errno_save = errno;

		printf("%s: read returned %d\n", __FILE__, errno);
		ret = -errno_save;
		goto out;
	}
	if (ret == 0) {
		printf("%s: EOF detected %d\n", __FILE__, errno);
		ret = -EINVAL;
		goto out;
	}

	printf("[ INFO ] count: start sending messages...\n");

	for (i = 0; i < MAX_COUNT; i++) {
		long j, sum = 0;
		struct timeval start, end;

		printf("[ INFO ] count: sending message #%d\n", i);

		ret = write(fd, &message, sizeof(int));
		if (ret == -1) {
			int errno_save = errno;

			printf("%s: read returned %d\n", __FILE__, errno);
			ret = -errno_save;
			goto out;
		}

		gettimeofday(&start, NULL);
		while (1) {
			long sec, usec;

			for (j = 0; j < (1UL << 20); j++) {
				sum += j;
			}
			if (sum < 0) {
				printf("%s: sum is negative (%ld)\n", __FILE__, sum);
				ret = -EINVAL;
				goto out;
			}

			gettimeofday(&end, NULL);

			usec = end.tv_sec - start.tv_sec;
			sec = usec < 0 ? -1 : 0;
			usec = usec < 0 ? usec + 1000000 : usec;
			sec += end.tv_sec - start.tv_sec;
			if (sec >= 1) {
				break;
			}
		}
	}

	ret = 0;
 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}
