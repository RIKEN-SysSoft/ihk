#include <errno.h>
#include <sys/wait.h>
#include <ihklib.h>

#include <user/ihklib_private.h>
#include <user/okng_user.h>

#include <blackbox/include/util.h>
#include <blackbox/include/cpu.h>
#include <blackbox/include/mem.h>
#include <blackbox/include/params.h>
#include <blackbox/include/linux.h>
#include <blackbox/include/msr.h>

int main(int argc, char **argv)
{
  int ret;
  int os_index;
  params_getopt(argc, argv);

  char fn[4096];
  /* Activate and check */
  /* Precondition */
  ret = linux_insmod(0);
  INTERR(ret, "linux_insmod returned %d\n", ret);

  sprintf(fn, "%s/kmod/test_driver.ko", QUOTE(CMAKE_INSTALL_PREFIX));
  ret = _linux_insmod(fn, NULL);

  int fd = ihklib_device_open(0);
  INTERR(fd < 0, "ihklib_device_open returned %d\n", fd);
  int test_mode = TEST_MCCTRL_PUT_PER_THREAD_DATA_UNSAFE;
  ret = ioctl(fd, IHK_DEVICE_SET_TEST_MODE, &test_mode);
  INTERR(ret, "ioctl IHK_DEVICE_SET_TEST_MODE returned %d. errno=%d\n", ret, -errno);
  close(fd); fd = -1;

  struct cpus cpus = { 0 };
  ret = _cpus_reserve(98, -1);
  INTERR(ret, "cpus_reserve returned %d\n", ret);

  ret = cpus_reserved(&cpus);
  INTERR(ret, "cpus_reserved returned %d\n", ret);

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

  int wstatus;
  char cmd[4096];
  sprintf(cmd, "%s/bin/mcexec "
    "%s/bin/check_cpu_register",
    QUOTE(WITH_MCK),
    QUOTE(CMAKE_INSTALL_PREFIX));
  ret = system(cmd);
  wstatus = WEXITSTATUS(ret);

  ret = linux_kill_mcexec();
  INTERR(ret, "linux_kill_mcexec returned %d\n", ret);

  out:
  ret = linux_kill_mcexec();
  INTERR(ret, "linux_kill_mcexec returned %d\n", ret);
  ihk_os_shutdown(0);
  os_wait_for_status(IHK_STATUS_INACTIVE);
  mems_os_release();
  cpus_os_release();

	if (ihk_get_num_os_instances(0)) {
    ihk_destroy_os(0, os_index);
  }
  cpus_release();
  mems_release();
  _linux_rmmod("test_driver");
  linux_rmmod(0);
  return ret;
}
