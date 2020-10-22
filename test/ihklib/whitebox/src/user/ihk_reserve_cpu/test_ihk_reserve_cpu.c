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

  /* Precondition */
  ret = linux_insmod(0);
  INTERR(ret, "linux_insmod returned %d\n", ret);

  char mode[6] = "\0";
  sprintf(mode, "%d", TEST_IHK_RESERVE_CPU);
  ret = setenv(IHKLIB_TEST_MODE_ENV_NAME, mode, 1);
  INTERR(ret, "setenv returned %d. errno=%d\n", ret, -errno);

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
  linux_rmmod(0);
  return ret;
}
