#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#define Op0_shift	19
#define Op0_mask	0x3
#define Op1_shift	16
#define Op1_mask	0x7
#define CRn_shift	12
#define CRn_mask	0xf
#define CRm_shift	8
#define CRm_mask	0xf
#define Op2_shift	5
#define Op2_mask	0x7

#define sys_reg(op0, op1, crn, crm, op2)	       \
	(((op0) << Op0_shift) | ((op1) << Op1_shift) | \
	 ((crn) << CRn_shift) | ((crm) << CRm_shift) | \
	 ((op2) << Op2_shift))

struct test_driver_ioctl_arg {
	unsigned long addr;
	unsigned long val;
	unsigned long addr_ext;
	int cpu;
	int fake_os;
	int fake_cpu;
};

int main(int argc, char **argv)
{
	int ret;
	int fd = -1;
  struct test_driver_ioctl_arg read_arg = { 0 };
  read_arg.addr_ext = sys_reg(3, 0, 11, 2, 0);

	printf("[ INFO ] addr_ext: %lx \n", read_arg.addr_ext);

	fd = open("/dev/test_driver", O_RDWR);
	if (fd == -1) {
		printf("[INTERR] open /dev/test_driver returned %d\n",
		       errno);
		ret = 1;
		goto out;
	}

	ret = ioctl(fd, 0, (unsigned long)&read_arg);

	if (ret) {
		printf("[INTERR] ioctl returned %d\n", -errno);
		ret = -errno;
		goto out;
	}

 out:
	if (fd != -1) {
		close(fd);
	}
	return ret;
}
