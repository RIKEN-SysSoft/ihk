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

const char param[] = "parameter string";
const char *values[] = {
	"MCK_MEM=ALL@0,ALL@1 with IHK_RESERVE_MEM_MAX_SIZE_RATIO_ALL of 95",
};
const int num_env[] = {
	4,
};
const char *env_str[] = {
	"MCK_CPUS=12-35\0"
#if FIRST_USER_NUMA == 4
	"MCK_MEM=ALL@4,ALL@5,ALL@6,ALL@7\0"
#else
	"MCK_MEM=ALL@0,ALL@1\0"
#endif
	"MCK_IKC_MAP=12-23:0+24-35:1\0"
	"MCK_KARGS=hidos allow_oversubscribe ihk_create_os_str14\0",
};
const int mem_conf_keys[] = {
	IHK_RESERVE_MEM_MAX_SIZE_RATIO_ALL
};
int mem_conf_values[] = {
	90
};
const char kernel_image[] = QUOTE(WITH_MCK) "/" QUOTE(BUILD_TARGET)
	"/kernel/mckernel.img";
const char default_kargs[] = "hidos allow_oversubscribe time_sharing";
const int ret_expected[] = {
	0,
};
struct mems mems_free_on_reserve[1];
struct mems mems_ratio[1];
const unsigned long mems_ratio_expected[1] = { 90 };
double ratios[1][MAX_NUM_MEM_CHUNKS];

char err_msg[IHKLIB_MAX_SIZE_ERR_MSG];
const char *err_msg_expected[] = {
	"",
};

int main(int argc, char **argv)
{
	int ret;
	int j;
	int subindex = 0;
	int os_index = -1;

	struct cpus cpus;

	ret = cpus_init(&cpus, 24);
	INTERR(ret, "cpus_init returned %d\n", ret);

	for (j = 0; j < 24; j++) {
		cpus.cpus[j] = j + 12;
	}

	struct ikc_cpu_map map_expected;

	ret = ikc_cpu_map_init(&map_expected, 24);
	INTERR(ret, "ikc_cpu_map_init returned %d\n", ret);

	for (j = 0; j < 12; j++) {
		map_expected.map[j].src_cpu = j + 12;
		map_expected.map[j].dst_cpu = 0;
	}
	for (; j < 24; j++) {
		map_expected.map[j].src_cpu = j + 12;
		map_expected.map[j].dst_cpu = 1;
	}

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	ret = ihk_reserve_mem_conf(0, mem_conf_keys[subindex],
				   &mem_conf_values[subindex]);
	INTERR(ret, "ihk_reserve_mem_conf failed with %d\n",
	       ret);

	START("test-case: %s: %s\n", param, values[subindex]);

	memset(err_msg, 0, IHKLIB_MAX_SIZE_ERR_MSG);
	ret = ihk_create_os_str(0, &os_index,
				env_str[subindex],
				num_env[subindex],
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
	     "err_msg: %s         expected: %s\n",
	     *err_msg ? err_msg : "(empty)\n",
	     *err_msg_expected[subindex] ?
	     err_msg_expected[subindex] : "(empty)");

	OKNG(os_index == 0 &&
	     ihk_get_num_os_instances(0) == 1,
	     "os instance #0 is created\n");

	/* Check OS status and clean up*/
	if (ihk_get_num_os_instances(0) > 0) {
		char kmsg[IHK_KMSG_SIZE] = { 0 };
		char *token;
		int excess;
		int num_mem_chunks;
		struct mems mems_dividend;

		ret = cpus_check_assigned(&cpus);
		OKNG(ret == 0, "cpu: assigned as expected\n");

		/* Scan Linux kmsg */
		ret = mems_free(&mems_free_on_reserve[subindex]);
		INTERR(ret, "mems_free returned %d\n", ret);

		excess = mems_free_on_reserve[subindex].num_mem_chunks - 4;
		if (excess > 0) {
			ret = mems_shift(&mems_free_on_reserve[subindex],
					 excess);
			INTERR(ret, "mems_shift returned %d\n", ret);
		}

		/* Check memory size measured as the ratio of free memory */
		ret = ihk_os_get_num_assigned_mem_chunks(0);
		INTERR(ret < 0, "ihk_get_num_reserved_mem_chunks returned %d\n",
		       ret);

		num_mem_chunks = ret;
		ret = mems_init(&mems_dividend, num_mem_chunks);
		INTERR(ret,
		       "mems_init failed with  %d, num_mem_chunks: %d\n",
		       ret, num_mem_chunks);

		ret = ihk_os_query_mem(0, mems_dividend.mem_chunks,
				       mems_dividend.num_mem_chunks);
		INTERR(ret, "ihk_os_query_mem returned %d\n",
		       ret);

		/* copy possibly non-contigous numa id distribution */
		ret = mems_copy(&mems_ratio[subindex],
				&mems_free_on_reserve[subindex]);
		INTERR(ret, "mems_copy returned %d\n", ret);

		mems_fill(&mems_ratio[subindex], mems_ratio_expected[subindex]);

		ret = _mems_check_ratio(&mems_dividend,
					&mems_free_on_reserve[subindex],
					&mems_ratio[subindex],
					ratios[subindex]);
		OKNG(ret == 0,
		     "reserved >= NR_FREE_PAGES * PAGE_SIZE * %.2f\n",
		     (double)mems_ratio_expected[subindex] / 100);

		ret = ikc_cpu_map_check(&map_expected);
		OKNG(ret == 0, "ikc_map: map set as expected\n");

		ret = ihk_os_boot(0);
		INTERR(ret, "ihk_os_boot returned %d\n", ret);

		ret = ihk_os_kmsg(0, kmsg, IHK_KMSG_SIZE);
		INTERR(ret < 0, "ihk_os_kmsg returned %d\n", ret);

		token = strstr(kmsg, "booted");
		OKNG(token, "\"booted\" found in kmsg\n");

		/* let the driver script check the kmsg */
		return 0;
	}

	ret = 0;
 out:
	if (ihk_get_num_os_instances(0) > 0) {
		ihk_os_shutdown(0);
		os_wait_for_status(IHK_STATUS_INACTIVE);
		cpus_os_release();
		mems_os_release();
		ihk_destroy_os(0, 0);
	}
	cpus_release();
	mems_release();
	linux_rmmod(1);

	return ret;
}
