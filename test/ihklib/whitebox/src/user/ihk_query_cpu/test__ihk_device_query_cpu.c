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

  ret = cpus_shift(&cpus_input, 94);
  INTERR(ret, "cpus_shift returned %d\n", ret);

  /* Precondition */
  ret = linux_insmod(0);
  INTERR(ret, "linux_insmod returned %d\n", ret);

  int fd = ihklib_device_open(0);
  INTERR(fd < 0, "ihklib_device_open returned %d\n", fd);
  int test_mode = TEST_IHK_DEVICE_QUERY_CPU;
  ret = ioctl(fd, IHK_DEVICE_SET_TEST_MODE, &test_mode);
  INTERR(ret, "ioctl IHK_DEVICE_SET_TEST_MODE returned %d. errno=%d\n", ret, -errno);
  close(fd); fd = -1;

  int reserved = 0;
  /* Activate and check */
  ret = ihk_reserve_cpu(0, cpus_input.cpus, cpus_input.ncpus);
  INTERR(ret, "ihk_reserve_cpu returned %d\n", ret);
  reserved = 1;

  int ncpus = ihk_get_num_reserved_cpus(0);
  ret = cpus_init(&cpus_input.cpus, ncpus);
  INTERR(ret, "cpus_init returned %d\n", ret);
  ret = ihk_query_cpu(0, cpus_input.cpus, cpus_input.ncpus);
  INTERR(ret, "ihk_query_cpu returned %d, expected: 0\n", ret);

  ret = 0;
 out:
  /* Clean up */
  if (fd >= 0) close(fd);
  if (reserved) {
    ihk_release_cpu(0, cpus_input.cpus, cpus_input.ncpus);
  }

  linux_rmmod(0);
  return ret;
}
