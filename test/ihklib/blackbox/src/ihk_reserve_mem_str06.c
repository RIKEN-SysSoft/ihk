#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ihklib.h>
#include <ihk/ihklib_private.h>
#include "util.h"
#include "okng.h"
#include "cpu.h"
#include "mem.h"
#include "os.h"
#include "params.h"
#include "linux.h"

const char param[] = "user privilege";
const char *values[] = {
	"non-root",
};

const char env_str[] =
	"IHK_CPUS=12-35\0"
	"IHK_MEM=1G@0,512M@1\0"
	"IHK_IKC_MAP=12-23:0+24-35:1\0"
	"IHK_KARGS=hidos allow_oversubscribe ihk_reserve_mem_str06\0";
const char kernel_image[] = QUOTE(WITH_MCK) "/" QUOTE(BUILD_TARGET)
	"/kernel/mckernel.img";
const char default_kargs[] = "hidos allow_oversubscribe time_sharing";
const int ret_expected[] = { -EACCES };
char err_msg[IHKLIB_MAX_SIZE_ERR_MSG];
const char *err_msg_expected[2] = {
	"ihk_get_num_reserved_cpus",
};

int main(int argc, char **argv)
{
	int ret;
	int subindex = 0;
	int os_index = -1;
	int opt;

	while ((opt = getopt(argc, argv, "ir")) != -1) {
		switch (opt) {
		case 'i':
			/* Precondition */
			ret = linux_insmod(0);
			INTERR(ret, "linux_insmod returned %d\n", ret);
			exit(0);
			break;
		case 'r':
			/* Check there's no side effects */
			OKNG(ihk_get_num_os_instances(0) == 0,
			     "no os instance created\n");
			OKNG(ihk_get_num_reserved_cpus(0) == 0,
			     "no cpus reserved\n");
			OKNG(ihk_get_num_reserved_mem_chunks(0) == 0,
			     "no memory reserved\n");

			/* Clean up */
			ret = linux_rmmod(0);
			INTERR(ret, "rmmod returned %d\n", ret);
			exit(0);
			break;
		default: /* '?' */
			printf("unknown option %c\n", optopt);
			exit(1);
		}
	}

	START("test-case: %s: %s\n", param, values[subindex]);

	memset(err_msg, 0, IHKLIB_MAX_SIZE_ERR_MSG);
	ret = ihk_create_os_str(0, &os_index,
				env_str, 4,
				kernel_image,
				default_kargs,
				err_msg);

	INFO("err_msg: %s",
	     *err_msg ? err_msg : "(empty)\n");

	OKNG(ret == ret_expected[subindex],
	     "return value: %d, expected: %d\n",
	     ret, ret_expected[subindex]);

	OKNG(strstr(*err_msg ? err_msg : "(empty)",
		    *err_msg_expected[subindex] ?
		    err_msg_expected[subindex] : "(empty)"),
	     "err_msg: %s\texpected: %s\n",
	     *err_msg ? err_msg : "(empty)\n",
	     *err_msg_expected[subindex] ?
	     err_msg_expected[subindex] : "(empty)");

	ret = 0;
 out:
	return ret;
}
