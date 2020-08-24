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
  sprintf(mode, "%d", TEST_IHK_OS_ASSIGN_CPU);
  ret = setenv(IHKLIB_TEST_MODE_ENV_NAME, mode, 1);
  INTERR(ret, "setenv returned %d. errno=%d\n", ret, -errno);

  /* Activate and check */
  /* Precondition */
  ret = linux_insmod(0);
  INTERR(ret, "linux_insmod returned %d\n", ret);

  ret = _cpus_reserve(98, -1);
  INTERR(ret, "cpus_reserve returned %d\n", ret);

  struct cpus cpus_input = { 0 };
  ret = cpus_reserved(&cpus_input);
  INTERR(ret, "cpus_reserved 1 returned %d\n", ret);

  struct cpus cpus_after_assign = { 0 };
  ret = cpus_reserved(&cpus_after_assign);
  INTERR(ret, "cpus_reserved 2 returned %d\n", ret);

  ret = ihk_create_os(0);
  INTERR(ret < 0, "ihk_create_os returned: %d\n", ret);
  os_index = ret;

  ret = ihk_os_assign_cpu(0, cpus_input.cpus, cpus_input.ncpus);
  INTERR(ret, "return value: %d, expected: %d\n", ret);

  ret = ihk_os_release_cpu(0, cpus_after_assign.cpus,
					      cpus_after_assign.ncpus);
	INTERR(ret, "ihk_os_release_cpu returned %d\n", ret);

 out:
  if (ihk_get_num_os_instances(0)) {
    ihk_destroy_os(0, os_index);
  }
  cpus_release();

  linux_rmmod(0);
  return ret;
}
