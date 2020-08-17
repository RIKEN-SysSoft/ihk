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

  /* Activate and check */
  /* Precondition */
  ret = linux_insmod(0);
  INTERR(ret, "linux_insmod returned %d\n", ret);

  int fd = ihklib_device_open(0);
  INTERR(fd < 0, "ihklib_device_open returned %d\n", fd);
  int test_mode = TEST_SMP_IHK_OS_SEND_NMI;
  ret = ioctl(fd, IHK_DEVICE_SET_TEST_MODE, &test_mode);
  INTERR(ret, "ioctl IHK_DEVICE_SET_TEST_MODE returned %d. errno=%d\n", ret, -errno);
  close(fd); fd = -1;

  ret = _cpus_reserve(98, -1);
  INTERR(ret, "cpus_reserve returned %d\n", ret);

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

 out:
  if (fd != -1) close(fd);
  ret = ihk_destroy_os(0, os_index);
  cpus_release();
  mems_release();
  linux_rmmod(0);
  return ret;
}
