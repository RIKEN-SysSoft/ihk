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

  struct cpus cpus_input = { 0 };

  /* All of McKernel CPUs */
  ret = cpus_ls(&cpus_input);
  INTERR(ret, "cpus_ls returned %d\n", ret);

  ret = cpus_shift(&cpus_input, 98);
  INTERR(ret, "cpus_shift returned %d\n", ret);

  /* Precondition */
  ret = linux_insmod(0);
  INTERR(ret, "linux_insmod returned %d\n", ret);

  int fd = ihklib_device_open(0);
  INTERR(fd < 0, "ihklib_device_open returned %d\n", fd);

  int test_mode = TEST_SMP_IHK_GET_NUM_CPUS;
  ret = ioctl(fd, IHK_DEVICE_SET_TEST_MODE, &test_mode);
  INTERR(ret, "ioctl IHK_DEVICE_SET_TEST_MODE returned %d. errno=%d\n", ret, -errno);
  close(fd); fd = -1;

  /* Activate and check */
  ret = ihk_reserve_cpu(0,
            cpus_input.cpus, cpus_input.ncpus);
  INTERR(ret, "ihk_reserve_cpu returned %d\n", ret);

  ret = ihk_get_num_reserved_cpus(0);
  INTERR(ret <= 0, "ihk_get_num_reserved_cpus returned %d\n", ret);

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
