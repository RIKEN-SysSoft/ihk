#include <errno.h>

#include <ihklib.h>
#include <user/ihklib_private.h>
#include <user/okng_user.h>

#include <blackbox/include/util.h>
#include <blackbox/include/cpu.h>
#include <blackbox/include/mem.h>
#include <blackbox/include/params.h>
#include <blackbox/include/linux.h>

#define INDEX_DUMMY -0x80000000

int main(int argc, char **argv)
{
  int ret;
  int os_index = 0;
  params_getopt(argc, argv);

  char mode[6] = "\0";
  sprintf(mode, "%d", TEST_IHK_GET_OS_INSTANCES);
  ret = setenv(IHKLIB_TEST_MODE_ENV_NAME, mode, 1);
  INTERR(ret, "setenv returned %d. errno=%d\n", ret, -errno);

  /* Activate and check */
  /* Precondition */
  ret = linux_insmod(0);
  INTERR(ret, "linux_insmod returned %d\n", ret);

  int index_input[1] = { INDEX_DUMMY };

  ret = ihk_create_os(0);
  INTERR(ret < 0, "ihk_create_os returned %d\n", ret);
  os_index = ret;

  ret = ihk_get_os_instances(0, index_input, 1);
  INTERR(ret != 0, "return value: %d, expected: %d\n", ret, 0);

 out:
  ihk_destroy_os(0, os_index);
  linux_rmmod(0);
  return ret;
}
