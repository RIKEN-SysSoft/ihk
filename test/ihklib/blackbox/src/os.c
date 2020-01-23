#include <stdio.h>
#include <ihklib.h>
#include <errno.h>
#include "util.h"
#include "okng.h"
#include "os.h"

int os_load(void)
{
	int ret;
	char fn[4096];

	sprintf(fn, "%s/%s/kernel/mckernel.img",
		QUOTE(WITH_MCK), QUOTE(BUILD_TARGET));
	ret = ihk_os_load(0, fn);
	INTERR(ret, "ihk_os_load returned %d\n", ret);

	ret = 0;
 out:
	return ret;
}

int os_kargs(void)
{
	int ret;
	char kargs[4096];

	sprintf(kargs, "hidos ksyslogd=0");
	ret = ihk_os_kargs(0, kargs);
	INTERR(ret, "ihk_os_kargs returned %d\n", ret);

	ret = 0;
 out:
	return ret;
}

int os_wait_for_status(int status)
{
	int ret;
	int i;

	INFO("waiting for status getting %d\n",
	     status);

	for (i = 0; i < 20; i++) {
		ret = ihk_os_get_status(0);
		if (ret == status) {
			ret = 0;
			goto out;
		}
		INFO("os status: %d\n", ret);
		usleep(400000);
	}
	ret = -ETIMEDOUT;
 out:
	return ret;
}
