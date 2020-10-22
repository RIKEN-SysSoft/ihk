#include <limits.h>
#include <errno.h>
#include <stdlib.h>

#include <ihklib.h>
#include <driver/ihk_host_user.h>
#include <user/okng_user.h>

#include <blackbox/include/util.h>
#include <blackbox/include/cpu.h>
#include <blackbox/include/params.h>
#include <blackbox/include/linux.h>

int main(int argc, char **argv)
{
  int ret;

  params_getopt(argc, argv);

  /* Precondition */
  ret = linux_insmod(0);
  INTERR(ret, "linux_insmod returned %d\n", ret);

  int fd = ihklib_device_open(0);
  INTERR(fd < 0, "ihklib_device_open returned %d\n", fd);
  int test_mode = TEST_CPU_ARRAY2STR;
  ret = ioctl(fd, IHK_DEVICE_SET_TEST_MODE, &test_mode);
  INTERR(ret, "ioctl IHK_DEVICE_SET_TEST_MODE returned %d. errno=%d\n", ret, -errno);
  close(fd); fd = -1;

  /* All of McKernel CPUs */
  struct cpus cpus_input = { 0 };

  ret = _cpus_ls(&cpus_input, "online", 98, -1);
  INTERR(ret, "_cpus_ls returned %d\n", ret);

  /* Activate and check */
  ret = ihk_reserve_cpu(0,
            cpus_input.cpus, cpus_input.ncpus);
  INTERR(ret, "ihk_reserve_cpu returned %d\n", ret);

  /* Clean up */
  ret = ihk_release_cpu(0, cpus_input.cpus,
            cpus_input.ncpus);
  INTERR(ret, "ihk_release_cpu returned %d\n", ret);

  ret = 0;
 out:
  if (fd >= 0) close(fd);
  linux_rmmod(0);
  return ret;
}
