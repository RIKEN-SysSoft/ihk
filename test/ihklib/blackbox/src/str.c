#define _GNU_SOURCE	 /* See feature_test_macros(7) */
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#define TEST_SZARRAY (1UL << 28)

int main(int argc, char **argv)
{
	int ret;
	int fd_in = -1, fd_out = -1;
	char *fn_in = NULL, *fn_out = NULL;
	int message;
	int opt;
	int kernel_mode = 0;
	long *test_array = NULL;
	int i;

	while ((opt = getopt(argc, argv, "i:o:k:")) != -1) {
		switch (opt) {
		case 'i':
			fn_in = optarg;
			break;
		case 'o':
			fn_out = optarg;
			break;
		case 'k':
			kernel_mode = atoi(optarg);
			break;
		default:
			printf("unknown option %c\n", optopt);
			ret = -EINVAL;
			goto out;
		}
	}

	fd_in = open(fn_in, O_RDWR);
	if (fd_in == -1) {
		int errno_save = errno;

		printf("%s: open %s returned %d\n",
		       __FILE__, fn_in, errno);
		ret = -errno_save;
		goto out;
	}

	fd_out = open(fn_out, O_RDWR);
	if (fd_out == -1) {
		int errno_save = errno;

		printf("%s: open %s returned %d\n",
		       __FILE__, fn_out, errno);
		ret = -errno_save;
		goto out;
	}

	if (kernel_mode) {
		ret = syscall(2001);
		if (ret) {
			printf("[INTERR] syscall 2001 returned %d\n",
			       errno);
			ret = -errno;
			goto sync_out;
		}
	} else {
		test_array = malloc(TEST_SZARRAY);
		memset((void *)test_array, 0xaa, TEST_SZARRAY);
	}

	/* Let parent start counter */
	ret = write(fd_out, &message, sizeof(int));
	if (ret != sizeof(int)) {
		int errno_save = errno;

		printf("%s: write returned %d, errno: %d\n",
		       __FILE__, ret, errno);
		ret = ret >= 0 ? ret : -errno_save;
		goto sync_out;
	}

	/* Wait until parent starts counter */
	ret = read(fd_in, &message, sizeof(int));
	if (ret != sizeof(int)) {
		int errno_save = errno;

		printf("%s: read returned %d, errno: %d\n",
		       __FILE__, ret, errno);
		ret = ret >= 0 ? ret : -errno_save;
		goto sync_out;
	}

	if (kernel_mode) {
		ret = syscall(2002);
		if (ret) {
			printf("[INTERR] syscall 2002 returned %d\n",
			       errno);
			ret = -errno;
			goto sync_out;
		}
	} else {
		for (i = 0; i < TEST_SZARRAY / sizeof(long); i++) {
			test_array[i] = (1UL << 63) - 1;
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

	/* Wait until parent takes stat */
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
	free(test_array);
	if (fd_in != -1) {
		close(fd_in);
	}
	if (fd_out != -1) {
		close(fd_out);
	}
	return ret;
}
