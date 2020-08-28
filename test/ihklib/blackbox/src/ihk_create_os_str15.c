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
	"cpus are reserved before called",
	"memory is reserved before called",
};

const char *env_str[] = {
	"MCK_CPUS=12-35\0"
#if FIRST_USER_NUMA == 4
	"MCK_MEM=1G@4,512M@5\0"
#else
	"MCK_MEM=1G@0,512M@1\0"
#endif
	"MCK_IKC_MAP=12-23:0+24-35:1\0"
	"MCK_KARGS=hidos allow_oversubscribe ihk_create_os_str15\0",
	"MCK_CPUS=12-35\0"
#if FIRST_USER_NUMA == 4
	"MCK_MEM=1G@4,512M@5\0"
#else
	"MCK_MEM=1G@0,512M@1\0"
#endif
	"MCK_IKC_MAP=12-23:0+24-35:1\0"
	"MCK_KARGS=hidos allow_oversubscribe ihk_create_os_str15\0",
};
const char kernel_image[] = QUOTE(WITH_MCK) "/" QUOTE(BUILD_TARGET)
	"/kernel/mckernel.img";
const char default_kargs[] = "hidos allow_oversubscribe time_sharing";
const int ret_expected[] = {
	0,
	0,
};
char err_msg[IHKLIB_MAX_SIZE_ERR_MSG];
const char *err_msg_expected[] = {
	"",
	"",
};

int main(int argc, char **argv)
{
	int ret;
	int j;
	int subindex = -1;
	int os_index = -1;
	int excess;
	int opt;

	opterr = 0;
	while ((opt = getopt(argc, argv, "i:")) != -1) {
		switch (opt) {
		case 'i':
			subindex = atoi(optarg);
			break;
		default:
			INTERR(1, "unknown option %c\n", opt);
			break;
		}
	}
	opterr = 1;

	INTERR(subindex == -1, "-i <subindex> is not specified\n");

	/* cpus took before calling ihk_create_os_str */
	struct cpus cpus_taken;

	ret = cpus_init(&cpus_taken, 12);
	INTERR(ret, "cpus_init returned %d\n", ret);

	for (j = 0; j < 12; j++) {
		cpus_taken.cpus[j] = j + 12;
	}

	/* memory took before calling ihk_create_os_str */
	struct mems mems_taken;

	ret = _mems_ls(&mems_taken, "MemFree", 0.9, -1);
	INTERR(ret, "mems_ls returned %d\n", ret);

	excess = mems_taken.num_mem_chunks - 4;
	if (excess > 0) {
		ret = mems_shift(&mems_taken, excess);
		INTERR(ret, "mems_ls returned %d\n", ret);
	}

	/* expected reservation */

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

	struct mems mems;
	struct mems mems_margin;

	ret = mems_init(&mems, 2);
	INTERR(ret, "mems_init returned %d\n", ret);

	mems.mem_chunks[0].size = 1UL << 30;
	mems.mem_chunks[1].size = 1UL << 29;

	for (j = 0; j < 2; j++) {
#if FIRST_USER_NUMA == 4
		mems.mem_chunks[j].numa_node_number = 4 + j;
#else
		mems.mem_chunks[j].numa_node_number = j;
#endif
	}

	ret = mems_copy(&mems_margin, &mems);
	INTERR(ret, "mems_copy returned %d\n", ret);

	mems_fill(&mems_margin, 4UL << 20);

	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	START("test-case: %s: %s\n", param, values[subindex]);

	if (subindex == 0) {
		ret = ihk_reserve_cpu(0, cpus_taken.cpus,
				      cpus_taken.ncpus);
		INTERR(ret, "ihk_reserve_cpu failed with %d\n",
		       ret);
	}

	if (subindex == 1) {
		ret = ihk_reserve_mem(0, mems_taken.mem_chunks,
				      mems_taken.num_mem_chunks);
		INTERR(ret, "ihk_reserve_mem failed with %d\n",
		       ret);
	}

	memset(err_msg, 0, IHKLIB_MAX_SIZE_ERR_MSG);
	ret = ihk_create_os_str(0, &os_index,
				env_str[subindex], 4,
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

	OKNG(os_index == 0 &&
	     ihk_get_num_os_instances(0) == 1,
	     "os instance #0 is created\n");

	/* Check OS status and clean up*/
	if (ihk_get_num_os_instances(0) > 0) {
		char kmsg[IHK_KMSG_SIZE] = { 0 };
		char *token;

		ret = cpus_check_assigned(&cpus);
		OKNG(ret == 0, "cpu: assigned as expected\n");

		ret = mems_check_assigned(&mems,
					  &mems_margin);
		OKNG(ret == 0, "mem: assigned as expected\n");

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
