#include <errno.h>

#include <ihklib.h>
#include <user/ihklib_private.h>
#include <user/okng_user.h>

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
  sprintf(mode, "%d", TEST_IHK_OS_FREEZE);
  ret = setenv(IHKLIB_TEST_MODE_ENV_NAME, mode, 1);
  INTERR(ret, "setenv returned %d. errno=%d\n", ret, -errno);

  /* Activate and check */
  /* Precondition */
  ret = linux_insmod(0);
  INTERR(ret, "linux_insmod returned %d\n", ret);

  ret = _cpus_reserve(98, -1);
  INTERR(ret, "cpus_reserve returned %d\n", ret);

  struct mems mems = { 0 };
  int excess;
  ret = _mems_ls(&mems, "MemFree", 0.02, -1);
  INTERR(ret, "mems_ls returned %d\n", ret);
  excess = mems.num_mem_chunks - 4;
  if (excess > 0) {
    ret = mems_shift(&mems, excess);
    INTERR(ret, "mems_shift returned %d\n", ret);
  }
  ret = ihk_reserve_mem(0, mems.mem_chunks, mems.num_mem_chunks);

  struct ikc_cpu_map map_input = { 0 };
  ret = ikc_cpu_map_2toN(&map_input);
  INTERR(ret, "ikc_cpu_map_2toN returned %d\n", ret);

  ret = ihk_create_os(0);
  INTERR(ret < 0, "ihk_create_os returned: %d\n", ret);

  os_index = ret;

  ret = cpus_os_assign();
  INTERR(ret, "cpus_os_assign returned %d\n", ret);

  ret = mems_os_assign();
  INTERR(ret, "mems_os_assign returned %d\n", ret);

  ret = ihk_os_set_ikc_map(0, map_input.map, map_input.ncpus);
  INTERR(ret, "ihk_os_set_ikc_map returned %d\n", ret);

  ret = os_load();
  INTERR(ret, "os_load returned %d\n", ret);

  ret = os_kargs();
  INTERR(ret, "os_kargs returned %d\n", ret);

  ret = ihk_os_boot(0);
  INTERR(ret, "ihk_os_boot returned %d\n", ret);

  ret = os_wait_for_status(IHK_STATUS_RUNNING);
  INTERR(ret, "os_wait_for_status timeout %d\n", ret);

  unsigned long os_set[1] = {1};
  INFO("trying to freeze os\n");
	ret = ihk_os_freeze(os_set, 8 * sizeof(unsigned long));
  INTERR(ret, "ihk_os_freeze returned %d\n", ret);

  ret = os_wait_for_status(IHK_STATUS_FROZEN);
  INTERR(ret, "os status didn't change to %d\n",
     IHK_STATUS_FROZEN);

  out:
  ihk_os_shutdown(0);
  os_wait_for_status(IHK_STATUS_INACTIVE);
  mems_os_release();
  cpus_os_release();

  if (ihk_get_num_os_instances(0))
    ihk_destroy_os(0, os_index);

  cpus_release();
  mems_release();
  linux_rmmod(0);
 }
