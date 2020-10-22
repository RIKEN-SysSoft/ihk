#include <limits.h>
#include <errno.h>
#include <stdlib.h>

#include <ihklib.h>
#include <user/ihklib_private.h>
#include <user/okng_user.h>

#include <blackbox/include/util.h>
#include <blackbox/include/cpu.h>
#include <blackbox/include/params.h>
#include <blackbox/include/linux.h>

int main(int argc, char **argv)
{
  int ret;

  params_getopt(argc, argv);

  int ret_expected = 0;

  /* Precondition */
  ret = linux_insmod(0);
  INTERR(ret, "linux_insmod returned %d\n", ret);

  char mode[6] = "\0";
  sprintf(mode, "%d", TEST_IHKLIB_DEVICE_OPEN);
  ret = setenv(IHKLIB_TEST_MODE_ENV_NAME, mode, 1);
  INTERR(ret, "setenv returned %d. errno=%d\n", ret, -errno);

  /* All of McKernel CPUs */
  struct cpus cpus_input = { 0 };

  ret = _cpus_ls(&cpus_input, "online", 98, -1);
  INTERR(ret, "_cpus_ls returned %d\n", ret);

  int fd = ihklib_device_open(0);
  
  INTERR(fd < 0, "ihklib_device_open returned %d\n", fd);
  close(fd);

  ret = 0;
 out:
  linux_rmmod(0);
  return ret;
}
