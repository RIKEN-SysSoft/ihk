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

  char mode[6] = "\0";
  sprintf(mode, "%d", TEST_IHK_OS_GET_EVENTFD);
  ret = setenv(IHKLIB_TEST_MODE_ENV_NAME, mode, 1);
  INTERR(ret, "setenv returned %d. errno=%d\n", ret, -errno);

  /* Activate and check */
  /* Precondition */
  ret = linux_insmod(0);
  INTERR(ret, "linux_insmod returned %d\n", ret);

  ret = ihk_create_os(0);
  INTERR(ret < 0, "ihk_create_os returned: %d\n", ret);

  os_index = ret;

  ret = ihk_os_get_eventfd(0, IHK_OS_EVENTFD_TYPE_OOM);
  INTERR(ret <= 0, "ihk_os_get_eventfd returned: %d\n", ret);

	ret = ihk_destroy_os(0, os_index);
  INTERR(ret, "ihk_destroy_os returned: %d\n", ret);

 out:
  linux_rmmod(0);
  return ret;
}
