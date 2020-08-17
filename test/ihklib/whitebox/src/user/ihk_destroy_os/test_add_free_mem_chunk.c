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

  /* Activate and check */
  /* Precondition */
  ret = linux_insmod(0);
  INTERR(ret, "linux_insmod returned %d\n", ret);

  int fd = ihklib_device_open(0);
  INTERR(fd < 0, "ihklib_device_open returned %d\n", fd);
  int test_mode = TEST_ADD_FREE_MEM_CHUNK;
  ret = ioctl(fd, IHK_DEVICE_SET_TEST_MODE, &test_mode);
  INTERR(ret, "ioctl IHK_DEVICE_SET_TEST_MODE returned %d. errno=%d\n", ret, -errno);
  close(fd); fd = -1;

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
  INTERR(ret, "ihk_reserve_mem return value: %d, expected: 0\n", ret);

  ret = ihk_create_os(0);
  INTERR(ret < 0, "return value (os index when positive): %d, expected: %d\n",
         ret, 0);
  os_index = ret;

  ret = ihk_os_assign_mem(0, mems.mem_chunks, mems.num_mem_chunks);
  INTERR(ret, "ihk_os_assign_mem return value: %d, expected: 0\n", ret);

  ret = mems_os_release();
  INTERR(ret, "mems_os_release returned %d\n", ret);

  ret = ihk_destroy_os(0, os_index);
  INTERR(ret, "ihk_destroy_os returned: %d\n", ret);

 out:
  mems_release();
  linux_rmmod(0);
  return ret;
}
