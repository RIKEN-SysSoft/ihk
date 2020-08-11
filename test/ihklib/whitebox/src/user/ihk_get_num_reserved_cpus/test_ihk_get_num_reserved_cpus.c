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

  struct cpus cpus_input = { 0 };

  /* All of McKernel CPUs */
  ret = cpus_ls(&cpus_input);
  INTERR(ret, "cpus_ls returned %d\n", ret);

  ret = cpus_shift(&cpus_input, 94);
  INTERR(ret, "cpus_shift returned %d\n", ret);

  /* Precondition */
  ret = linux_insmod(0);
  INTERR(ret, "linux_insmod returned %d\n", ret);


  char mode[6] = "\0";
  sprintf(mode, "%d", TEST_IHK_GET_NUM_RESERVED_CPUS);
  ret = setenv(IHKLIB_TEST_MODE_ENV_NAME, mode, 1);
  INTERR(ret, "setenv returned %d. errno=%d\n", ret, -errno);

  /* Activate and check */
  ret = ihk_reserve_cpu(0,
            cpus_input.cpus, cpus_input.ncpus);
  INTERR(ret, "ihk_reserve_cpu returned %d\n", ret);

  /* check func */
  ret = ihk_get_num_reserved_cpus(0);
  INTERR(ret <= 0, "ihk_get_num_reserved_cpus returned %d\n", ret);

  /* Clean up */
  ret = ihk_release_cpu(0, cpus_input.cpus,
            cpus_input.ncpus);
  INTERR(ret, "ihk_release_cpu returned %d\n", ret);

  ret = 0;
 out:
  linux_rmmod(0);
  return ret;
}
