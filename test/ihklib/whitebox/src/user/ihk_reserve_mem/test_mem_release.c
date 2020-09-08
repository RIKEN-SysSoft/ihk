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
  ret = _mems_ls(&mems_input, "MemFree", 0.01, 1UL << 30);
  INTERR(ret, "mems_ls returned %d\n", ret);

  excess = mems_input.num_mem_chunks - 4;
  if (excess > 0) {
    ret = mems_shift(&mems_input, excess);
    INTERR(ret, "mems_ls returned %d\n", ret);
  }

  struct mems mems_margin = { 0 };

  ret = _mems_ls(&mems_margin, "MemFree", 0.01, 1UL << 30);
  INTERR(ret, "mems_ls returned %d\n", ret);

  excess = mems_margin.num_mem_chunks - 4;
  if (excess > 0) {
    ret = mems_shift(&mems_margin, excess);
    INTERR(ret, "mems_shift returned %d\n", ret);
  }

  mems_fill(&mems_margin, 4UL << 20);

  struct mems *mems_expected = &mems_input;

  // char mode[6] = "\0";
  // sprintf(mode, "%d", TEST_IHK_RESERVE_MEM);
  // ret = setenv(IHKLIB_TEST_MODE_ENV_NAME, mode, 1);
  // INTERR(ret, "setenv returned %d. errno=%d\n", ret, -errno);

  /* Activate and check */
  /* Precondition */
  ret = linux_insmod(0);
  INTERR(ret, "linux_insmod returned %d\n", ret);

  int allowed_var = 10;
	ret = ihk_reserve_mem_conf(0, IHK_RESERVE_MEM_TOTAL, &allowed_var);
	INTERR(ret, "ihk_reserve_mem_conf returned %d\n", ret);
  int ratio = 30;
  ret = ihk_reserve_mem_conf(0, IHK_RESERVE_MEM_MAX_SIZE_RATIO_ALL, &ratio);

  int i;
  for (i = 0; i < 20; i++) {
    sleep(5);
    printf("loop %d\n", i);
    ret = ihk_reserve_mem(0, mems_input.mem_chunks, mems_input.num_mem_chunks);
    INTERR(ret, "return value: %d, expected: 0\n", ret);

    //ret = mems_check_reserved(mems_expected, &mems_margin);
    //INTERR(ret, "cannot reserve as expected\n");

    //ret = mems_release();
    struct ihk_mem_chunk mem_chunks[1] = {
      { .size = -1UL, .numa_node_number = 0 }
    };

    ret = ihk_release_mem(0, mem_chunks, 1);
    if (ret) printf("Can not release reserved mem\n");
  }

  ret = 0;
 out:
 /* Clean up */
  //ret = mems_release();
  /*struct ihk_mem_chunk mem_chunks[1] = {
    { .size = -1UL, .numa_node_number = 0 }
  };

  ret = ihk_release_mem(0, mem_chunks, 1);*/

  //if (ret) PRINT("ihk_release_mem returned %d\n", ret);

  linux_rmmod(0);
  return ret;
}
