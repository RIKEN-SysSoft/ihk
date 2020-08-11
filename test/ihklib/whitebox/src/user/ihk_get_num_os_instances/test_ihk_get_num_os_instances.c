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

  params_getopt(argc, argv);

  char mode[6] = "\0";
  sprintf(mode, "%d", TEST_IHK_GET_NUM_OS_INSTANCES);
  ret = setenv(IHKLIB_TEST_MODE_ENV_NAME, mode, 1);
  INTERR(ret, "setenv returned %d. errno=%d\n", ret, -errno);

  /* Activate and check */
  /* Precondition */
  ret = linux_insmod(0);
  INTERR(ret, "linux_insmod returned %d\n", ret);

  int old_num_instance = ihk_get_num_os_instances(0);
//	INTERR(ret == 1, "# of os instances: %d, expected: %d\n", ret, 1);

  ret = ihk_create_os(0);
  INTERR(ret, "ihk_create_os returned %d\n", ret);

  int new_num_instance = ihk_get_num_os_instances(0);
	INTERR(new_num_instance != old_num_instance + 1,
    "# of os instances: %d, expected: %d\n", new_num_instance, old_num_instance);

 out:

  linux_rmmod(0);
  return ret;
}
