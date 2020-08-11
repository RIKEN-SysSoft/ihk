#include <errno.h>

#include <ihklib.h>
#include <user/ihklib_private.h>
#include <user/okng_user.h>

#include <blackbox/include/util.h>
#include <blackbox/include/cpu.h>
#include <blackbox/include/mem.h>
#include <blackbox/include/params.h>
#include <blackbox/include/linux.h>

int main(int argc, char **argv)
{
  int ret;
  int os_index;
  params_getopt(argc, argv);

  /* Activate and check */
  /* Precondition */
  ret = linux_insmod(0);
  INTERR(ret, "linux_insmod returned %d\n", ret);

  int fd = ihklib_device_open(0);
  INTERR(fd < 0, "ihklib_device_open returned %d\n", fd);
  int test_mode = TEST_DELETE_KMSG_BUF;
  ret = ioctl(fd, IHK_DEVICE_SET_TEST_MODE, &test_mode);
  INTERR(ret, "ioctl IHK_DEVICE_SET_TEST_MODE returned %d. errno=%d\n", ret, -errno);
  close(fd); fd = -1;

  int old_num_instance = ihk_get_num_os_instances(0);

  ret = ihk_create_os(0);
  INTERR(ret < 0, "return value (os index when positive): %d, expected: %d\n",
		     ret, 0);

  os_index = ret;

  ret = ihk_get_num_os_instances(0);
  INTERR(ret != old_num_instance + 1, "# of os instances: %d, expected: %d\n",
		   ret, old_num_instance + 1);

  if (ihk_get_num_os_instances(0)) {
		ret = ihk_destroy_os(0, os_index);
	}

 out:
  linux_rmmod(0);
  return ret;
}
