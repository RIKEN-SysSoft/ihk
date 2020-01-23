#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

struct test_driver_ioctl_arg {
	unsigned long addr;
	unsigned long val;
	unsigned long addr_ext;
	int cpu;
	int fake_os;
	int fake_cpu;
};

const char *fake_os_list[] = {
	"valid one",
	"NULL",
	"non-existent one"
};

int main(int argc, char **argv)
{
	int ret;
	int fd = -1;
	int opt;
	int cpu_expected = -1;
	int fail = 0;
	int fake_os = 0, fake_cpu = 0;
	int errno_expected = 0;
	int write_first = 0;

	struct test_driver_ioctl_arg read_arg = { 0 };
	struct test_driver_ioctl_arg write_arg = { 0 };

	while ((opt = getopt(argc, argv, "a:c:f:F:e:w")) != -1) {
		switch (opt) {
		case 'a':
			read_arg.addr_ext = atol(optarg);
			write_arg.addr_ext = atol(optarg);
			break;
		case 'c':
			cpu_expected = atoi(optarg);
			break;
		case 'f':
			fake_os = 1;
			read_arg.fake_os = atoi(optarg);
			write_arg.fake_os = atoi(optarg);
			break;
		case 'F':
			fake_cpu = 1;
			read_arg.fake_cpu = 1;
			read_arg.cpu = atoi(optarg);
			write_arg.fake_cpu = 1;
			write_arg.cpu = atoi(optarg);
			break;
		case 'e':
			errno_expected = atoi(optarg);
			break;
		case 'w':
			write_first = 1;
			break;
		default: /* '?' */
			printf("unknown option %c\n", optopt);
			ret = -EINVAL;
			goto out;
		}
	}

	printf("[ INFO ] addr_ext: %lx, cpu_expected: %d, "
	       "fake_os: %d (%s), "
	       "fake_cpu: %d (disguise as %d), "
	       "errno_expected: %d\n",
	       read_arg.addr_ext, cpu_expected,
	       fake_os, fake_os_list[read_arg.fake_os],
	       fake_cpu, read_arg.cpu,
	       errno_expected);

	fd = open("/dev/test_driver", O_RDWR);
	if (fd == -1) {
		printf("[INTERR] open /dev/test_driver returned %d\n",
		       errno);
		ret = 1;
		goto out;
	}

	if (write_first) {
		goto write_first;
	}

	ret = ioctl(fd, 0, (unsigned long)&read_arg);

	if (fake_os || fake_cpu) {
		int errno_save = errno;

		if (errno_save == errno_expected) {
			printf("[  OK  ] ");
		} else {
			printf("[  NG  ] ");
			fail++;
		}
		printf("ihk_get_request_os_cpu: returned: %d, expected: %d\n",
		       errno_save, errno_expected);
		goto skip_rmw;
	}

	if (ret) {
		printf("[INTERR] ioctl 1st read returned %d\n",
		       errno);
		ret = -errno;
		goto out;
	}

	if (cpu_expected != -1) {
		if (read_arg.cpu == cpu_expected) {
			printf("[  OK  ] ");
		} else {
			printf("[  NG  ] ");
			fail++;
		}
		printf("ikc-channel source cpu: returned: %d, expected: %d\n",
		       read_arg.cpu, cpu_expected);
	}

 write_first:
	write_arg.val = (read_arg.val ^ 0x1);
	ret = ioctl(fd, 1, (unsigned long)&write_arg);

	if (fake_os || fake_cpu) {
		int errno_save = errno;

		if (errno_save == errno_expected) {
			printf("[  OK  ] ");
		} else {
			printf("[  NG  ] ");
			fail++;
		}
		printf("ihk_get_request_os_cpu: returned: %d, expected: %d\n",
		       errno_save, errno_expected);
		goto skip_rmw;
	}

	if (ret) {
		printf("[INTERR] ioctl write returned %d\n",
		       errno);
		ret = -errno;
		goto out;
	}

	ret = ioctl(fd, 0, (unsigned long)&read_arg);
	if (ret) {
		printf("[INTERR] ioctl 2nd read returned %d\n",
		       errno);
		ret = -errno;
		goto out;
	}

	if (read_arg.val == write_arg.val) {
		printf("[  OK  ] ");
	} else {
		printf("[  NG  ] ");
		fail++;
	}
	printf("read-modify-write\n");

 skip_rmw:
	ret = fail == 0 ? 0 : 1;
 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}
