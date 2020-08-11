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

  // char mode[6] = "\0";
  // sprintf(mode, "%d", TEST_IHK_CREATE_OS);
  // ret = setenv(IHKLIB_TEST_MODE_ENV_NAME, mode, 1);
  // INTERR(ret, "setenv returned %d. errno=%d\n", ret, -errno);

  /* Activate and check */
  /* Precondition */
  ret = linux_insmod(0);
  INTERR(ret, "linux_insmod returned %d\n", ret);

  int old_num_instance = ihk_get_num_os_instances(0);

  ret = cpus_reserve();
INTERR(ret, "cpus_reserve returned %d\n", ret);

ret = mems_reserve();
INTERR(ret, "mems_reserve returned %d\n", ret);

  ret = ihk_create_os(0);
  INTERR(ret < 0, "return value (os index when positive): %d, expected: %d\n",
		     ret, 0);

//  ret = ihk_create_os(0);
//  INTERR(ret < 0, "return value (os index when positive): %d, expected: %d\n",
//        ret, 0);

  os_index = ret;


  ret = cpus_os_assign();
	INTERR(ret, "cpus_os_assign returned %d\n", ret);

	ret = mems_os_assign();
	INTERR(ret, "mems_os_assign returned %d\n", ret);

	ret = os_load();
	INTERR(ret, "os_load returned %d\n", ret);

	ret = os_kargs();
	INTERR(ret, "os_kargs returned %d\n", ret);

  ret = ihk_os_boot(0);
  printf (" ret:  %d \n", ret);
//  ret = ihk_get_num_os_instances(0);
//  INTERR(ret != old_num_instance + 1, "# of os instances: %d, expected: %d\n",
//		   ret, old_num_instance + 1);


 out:

//  linux_rmmod(0);
  return ret;
}
