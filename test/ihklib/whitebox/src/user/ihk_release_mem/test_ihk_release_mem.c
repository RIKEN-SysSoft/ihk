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

  struct mems mems_input = { 0 };

  int excess;
  ret = mems_ls(&mems_input, "MemFree", 0.02);
  INTERR(ret, "mems_ls returned %d\n", ret);

  excess = mems_input.num_mem_chunks - 4;
  if (excess > 0) {
    ret = mems_shift(&mems_input, excess);
    INTERR(ret, "mems_ls returned %d\n", ret);
  }


  char mode[6] = "\0";
  sprintf(mode, "%d", TEST_IHK_RELEASE_MEM);
  ret = setenv(IHKLIB_TEST_MODE_ENV_NAME, mode, 1);
  INTERR(ret, "setenv returned %d. errno=%d\n", ret, -errno);

  /* Activate and check */
  /* Precondition */
  ret = linux_insmod(0);
  INTERR(ret, "linux_insmod returned %d\n", ret);

  int allowed_var = 10;
	ret = ihk_reserve_mem_conf(0, IHK_RESERVE_MEM_TOTAL, &allowed_var);
	INTERR(ret, "ihk_reserve_mem_conf returned %d\n", ret);
  int ratio = 10;
  ret = ihk_reserve_mem_conf(0, IHK_RESERVE_MEM_MAX_SIZE_RATIO_ALL, &ratio);
  INTERR(ret, "ihk_reserve_mem_conf returned %d\n", ret);

  ret = ihk_reserve_mem(0, mems_input.mem_chunks, mems_input.num_mem_chunks);
  INTERR(ret, "return value: %d, expected: 0\n", ret);

  ret = ihk_get_num_reserved_mem_chunks(0);
	INTERR(ret < 0, "ihk_get_num_reserved_mem_chunks returned %d\n", ret);
  struct mems _mems_release = { 0 };

  ret = mems_init(&_mems_release, ret);
	INTERR(ret, "mems_init returned %d\n", ret);

  ret = ihk_query_mem(0, _mems_release.mem_chunks, _mems_release.num_mem_chunks);
  INTERR(ret, "ihk_query_mem returned %d\n", ret);

  ret = mems_shift(&_mems_release, 1);
  INTERR(ret, "mems_shift returned %d\n", ret);
  
  ret = ihk_release_mem(0, _mems_release.mem_chunks, _mems_release.num_mem_chunks);
	INTERR(ret != 0, "return value: %d, expected: %d\n", ret, 0);

  ret = mems_release();
 out:

  linux_rmmod(0);
  return ret;
}
