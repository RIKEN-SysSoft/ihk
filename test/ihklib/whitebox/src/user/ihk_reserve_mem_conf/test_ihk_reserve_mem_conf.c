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
  sprintf(mode, "%d", TEST_IHK_RESERVE_MEM_CONF);
  ret = setenv(IHKLIB_TEST_MODE_ENV_NAME, mode, 1);
  INTERR(ret, "setenv returned %d. errno=%d\n", ret, -errno);

  int mem_conf_keys = IHK_RESERVE_MEM_MAX_SIZE_RATIO_ALL;
  int mem_conf_values = 95;

  ret = ihk_reserve_mem_conf(0, mem_conf_keys, &mem_conf_values);
  INTERR(ret, "ihk_reserve_mem_conf returned %d\n", ret);
  ret = 0;

 out:
  linux_rmmod(0);
  return ret;
}
