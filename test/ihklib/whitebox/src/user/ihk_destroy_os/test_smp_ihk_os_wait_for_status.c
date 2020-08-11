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

  ret = ihk_create_os(0);
  INTERR(ret < 0, "ihk_create_os returned: %d\n", ret);

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
  INTERR(ret, "ihk_os_boot returned %d\n", ret);

  int fd = ihklib_os_open(0);
  INTERR(fd < 0, "ihklib_os_open returned %d\n", fd);
  ret = ioctl(fd, IHK_OS_WAIT_FOR_STATUS);
  INTERR(ret, "ioctl returned: %d\n", ret);
  close(fd);

 out:
  ret = ihk_destroy_os(0, os_index);
  linux_rmmod(0);
  return ret;
}
