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
  sprintf(mode, "%d", TEST_IHK_OS_QUERY_MEM);
  ret = setenv(IHKLIB_TEST_MODE_ENV_NAME, mode, 1);
  INTERR(ret, "setenv returned %d. errno=%d\n", ret, -errno);

  /* Activate and check */
  /* Precondition */
  ret = linux_insmod(0);
  INTERR(ret, "linux_insmod returned %d\n", ret);

  struct mems mems = { 0 };
  int excess;
  ret = mems_ls(&mems, "MemFree", 0.02);
  INTERR(ret, "mems_ls returned %d\n", ret);
  excess = mems.num_mem_chunks - 4;
  if (excess > 0) {
    ret = mems_shift(&mems, excess);
    INTERR(ret, "mems_shift returned %d\n", ret);
  }
  ret = ihk_reserve_mem(0, mems.mem_chunks, mems.num_mem_chunks);

  struct mems mems_input = { 0 };
  ret = mems_reserved(&mems_input);
	INTERR(ret, "mems_reserved returned %d\n", ret);

  ret = ihk_create_os(0);
  INTERR(ret < 0, "ihk_create_os returned: %d\n", ret);
  os_index = ret;

  ret = ihk_os_assign_mem(0, mems_input.mem_chunks, mems_input.num_mem_chunks);
	INTERR(ret, "ihk_os_assign_mem return value: %d, expected: %d\n", ret, 0);

  ret = ihk_os_query_mem(0, mems_input.mem_chunks, mems_input.num_mem_chunks);
  INTERR(ret, "ihk_os_query_mem return value: %d, expected: %d\n", ret, 0);

  ret = mems_os_release();
  INTERR(ret, "mems_os_release returned %d\n", ret);

 out:
  if (ihk_get_num_os_instances(0)) {
    ihk_destroy_os(0, os_index);
  }
  mems_release();

  linux_rmmod(0);
  return ret;
}
