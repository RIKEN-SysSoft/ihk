#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <ihk/ihk_host_user.h>

#define OKNG(cond, fmt, args...) do {		\
	if (cond) {						\
		printf("[OK] " fmt, ##args);	\
	} else {						\
		printf("[NG] " fmt, ##args);		\
		goto out;			\
	}							\
} while (0)

int
main(int argc, char *argv[])
{
	int ret = 0, tid = 1, rc;
	int mckfd = 0;
	struct ihk_cpu_req cpu_req_nega_num;
	struct ihk_cpu_req cpu_req_null_cpus;
	struct ihk_ikc_req ikc_req_nega_num;
	struct ihk_ikc_req ikc_req_null_cpus;
	struct ihk_mem_req mem_req_ok;
	struct ihk_mem_req mem_req_nega_num;
	struct ihk_mem_req mem_req_null_mems;
	struct ihk_mem_req mem_req_nega_chunk_size;
	struct ihk_mem_req mem_req_nega_ratio;
	struct ihk_mem_req mem_req_over_ratio;
	int dummy;

	cpu_req_nega_num.num_cpus = -1;
	cpu_req_nega_num.cpus = &dummy;
	cpu_req_null_cpus.num_cpus = 10;
	cpu_req_null_cpus.cpus = NULL;

	ikc_req_nega_num.num_cpus = -1;
	ikc_req_nega_num.src_cpus = &dummy;
	ikc_req_nega_num.dst_cpus = &dummy;
	ikc_req_null_cpus.num_cpus = 10;
	ikc_req_null_cpus.src_cpus = NULL;
	ikc_req_null_cpus.dst_cpus = NULL;

	mem_req_ok.num_chunks = 10;
	mem_req_ok.sizes = (size_t *)&dummy;
	mem_req_ok.numa_ids = &dummy;
	mem_req_ok.min_chunk_size = 4096;
	mem_req_ok.max_size_ratio_all = 90;
	mem_req_ok.timeout = 1;

	mem_req_nega_num = mem_req_ok;
	mem_req_nega_num.num_chunks = -1;

	mem_req_null_mems = mem_req_ok;
	mem_req_null_mems.sizes = NULL;
	mem_req_null_mems.numa_ids = NULL;

	mem_req_nega_chunk_size = mem_req_ok;
	mem_req_nega_chunk_size.min_chunk_size = -1;

	mem_req_nega_ratio = mem_req_ok;
	mem_req_nega_ratio.max_size_ratio_all = -1;

	mem_req_over_ratio = mem_req_ok;
	mem_req_over_ratio.max_size_ratio_all = 105;

	// Open mcd0
	mckfd = open("/dev/mcd0", O_RDONLY);
	if (mckfd == -1) {
		printf("Error: failed to open /dev/mcd0\n");
		ret = -1;
		goto out;
	}

	// Test for reserve cpu
	rc = ioctl(mckfd, IHK_DEVICE_RESERVE_CPU, &cpu_req_nega_num);
	OKNG(rc != 0, "CT%03d: reserve_cpu by negative num_cpus\n", tid);
	tid++;

	rc = ioctl(mckfd, IHK_DEVICE_RESERVE_CPU, &cpu_req_null_cpus);
	OKNG(rc != 0, "CT%03d: reserve_cpu by NULL array\n", tid);
	tid++;

	// Test for release cpu
	rc = ioctl(mckfd, IHK_DEVICE_RELEASE_CPU, &cpu_req_nega_num);
	OKNG(rc != 0, "CT%03d: release_cpu by negative num_cpus\n", tid);
	tid++;

	rc = ioctl(mckfd, IHK_DEVICE_RELEASE_CPU, &cpu_req_null_cpus);
	OKNG(rc != 0, "CT%03d: release_cpu by NULL array\n", tid);
	tid++;

	// Test for query cpu
	rc = ioctl(mckfd, IHK_DEVICE_QUERY_CPU, &cpu_req_nega_num);
	OKNG(rc != 0, "CT%03d: query_cpu by negative num_cpus\n", tid);
	tid++;

	rc = ioctl(mckfd, IHK_DEVICE_QUERY_CPU, &cpu_req_null_cpus);
	OKNG(rc != 0, "CT%03d: query_cpu by NULL array\n", tid);
	tid++;

	// Test for os_assign cpu
	rc = ioctl(mckfd, IHK_OS_ASSIGN_CPU, &cpu_req_nega_num);
	OKNG(rc != 0, "CT%03d: os_assign_cpu by negative num_cpus\n", tid);
	tid++;

	rc = ioctl(mckfd, IHK_OS_ASSIGN_CPU, &cpu_req_null_cpus);
	OKNG(rc != 0, "CT%03d: os_assign_cpu by NULL array\n", tid);
	tid++;

	// Test for os_release cpu
	rc = ioctl(mckfd, IHK_OS_RELEASE_CPU, &cpu_req_nega_num);
	OKNG(rc != 0, "CT%03d: os_release_cpu by negative num_cpus\n", tid);
	tid++;

	rc = ioctl(mckfd, IHK_OS_RELEASE_CPU, &cpu_req_null_cpus);
	OKNG(rc != 0, "CT%03d: os_release_cpu by NULL array\n", tid);
	tid++;

	// Test for os_query cpu
	rc = ioctl(mckfd, IHK_OS_QUERY_CPU, &cpu_req_nega_num);
	OKNG(rc != 0, "CT%03d: os_query_cpu by negative num_cpus\n", tid);
	tid++;

	rc = ioctl(mckfd, IHK_OS_QUERY_CPU, &cpu_req_null_cpus);
	OKNG(rc != 0, "CT%03d: os_query_cpu by NULL array\n", tid);
	tid++;

	// Test for os_set_ikc_map
	rc = ioctl(mckfd, IHK_OS_SET_IKC_MAP, &ikc_req_nega_num);
	OKNG(rc != 0, "CT%03d: os_set_ikc_map by negative num_cpus\n", tid);
	tid++;

	rc = ioctl(mckfd, IHK_OS_SET_IKC_MAP, &ikc_req_null_cpus);
	OKNG(rc != 0, "CT%03d: os_set_ikc_map by NULL array\n", tid);
	tid++;

	// Test for os_get_ikc_map
	rc = ioctl(mckfd, IHK_OS_GET_IKC_MAP, &ikc_req_nega_num);
	OKNG(rc != 0, "CT%03d: os_get_ikc_map by negative num_cpus\n", tid);
	tid++;

	rc = ioctl(mckfd, IHK_OS_GET_IKC_MAP, &ikc_req_null_cpus);
	OKNG(rc != 0, "CT%03d: os_get_ikc_map by NULL array\n", tid);
	tid++;

	// Test for reserve mem
	rc = ioctl(mckfd, IHK_DEVICE_RESERVE_MEM, &mem_req_nega_num);
	OKNG(rc != 0, "CT%03d: reserve_mem by negative num_mems\n", tid);
	tid++;

	rc = ioctl(mckfd, IHK_DEVICE_RESERVE_MEM, &mem_req_null_mems);
	OKNG(rc != 0, "CT%03d: reserve_mem by NULL array\n", tid);
	tid++;

	rc = ioctl(mckfd, IHK_DEVICE_RESERVE_MEM, &mem_req_nega_chunk_size);
	OKNG(rc != 0, "CT%03d: reserve_mem by negative min_chunk_size\n", tid);
	tid++;

	rc = ioctl(mckfd, IHK_DEVICE_RESERVE_MEM, &mem_req_nega_ratio);
	OKNG(rc != 0, "CT%03d: reserve_mem by negative ratio\n", tid);
	tid++;

	rc = ioctl(mckfd, IHK_DEVICE_RESERVE_MEM, &mem_req_over_ratio);
	OKNG(rc != 0, "CT%03d: reserve_mem by over ratio\n", tid);
	tid++;

	// Test for release mem
	rc = ioctl(mckfd, IHK_DEVICE_RELEASE_MEM, &mem_req_nega_num);
	OKNG(rc != 0, "CT%03d: release_mem by negative num_mems\n", tid);
	tid++;

	rc = ioctl(mckfd, IHK_DEVICE_RELEASE_MEM, &mem_req_null_mems);
	OKNG(rc != 0, "CT%03d: release_mem by NULL array\n", tid);
	tid++;

	rc = ioctl(mckfd, IHK_DEVICE_RELEASE_MEM, &mem_req_nega_chunk_size);
	OKNG(rc != 0, "CT%03d: release_mem by negative min_chunk_size\n", tid);
	tid++;

	rc = ioctl(mckfd, IHK_DEVICE_RELEASE_MEM, &mem_req_nega_ratio);
	OKNG(rc != 0, "CT%03d: release_mem by negative ratio\n", tid);
	tid++;

	rc = ioctl(mckfd, IHK_DEVICE_RELEASE_MEM, &mem_req_over_ratio);
	OKNG(rc != 0, "CT%03d: release_mem by over ratio\n", tid);
	tid++;

	// Test for release mem
	rc = ioctl(mckfd, IHK_DEVICE_RELEASE_MEM_PARTIALLY, &mem_req_nega_num);
	OKNG(rc != 0, "CT%03d: release_mem_partially by negative num_mems\n", tid);
	tid++;

	rc = ioctl(mckfd, IHK_DEVICE_RELEASE_MEM_PARTIALLY, &mem_req_null_mems);
	OKNG(rc != 0, "CT%03d: release_mem_partially by NULL array\n", tid);
	tid++;

	rc = ioctl(mckfd, IHK_DEVICE_RELEASE_MEM_PARTIALLY, &mem_req_nega_chunk_size);
	OKNG(rc != 0, "CT%03d: release_mem_partially by negative min_chunk_size\n", tid);
	tid++;

	rc = ioctl(mckfd, IHK_DEVICE_RELEASE_MEM_PARTIALLY, &mem_req_nega_ratio);
	OKNG(rc != 0, "CT%03d: release_mem_partially by negative ratio\n", tid);
	tid++;

	rc = ioctl(mckfd, IHK_DEVICE_RELEASE_MEM_PARTIALLY, &mem_req_over_ratio);
	OKNG(rc != 0, "CT%03d: release_mem_partially by over ratio\n", tid);
	tid++;

	// Test for query mem
	rc = ioctl(mckfd, IHK_DEVICE_QUERY_MEM, &mem_req_nega_num);
	OKNG(rc != 0, "CT%03d: query_mem by negative num_mems\n", tid);
	tid++;

	rc = ioctl(mckfd, IHK_DEVICE_QUERY_MEM, &mem_req_null_mems);
	OKNG(rc != 0, "CT%03d: query_mem by NULL array\n", tid);
	tid++;

	rc = ioctl(mckfd, IHK_DEVICE_QUERY_MEM, &mem_req_nega_chunk_size);
	OKNG(rc != 0, "CT%03d: query_mem by negative min_chunk_size\n", tid);
	tid++;

	rc = ioctl(mckfd, IHK_DEVICE_QUERY_MEM, &mem_req_nega_ratio);
	OKNG(rc != 0, "CT%03d: query_mem by negative ratio\n", tid);
	tid++;

	rc = ioctl(mckfd, IHK_DEVICE_QUERY_MEM, &mem_req_over_ratio);
	OKNG(rc != 0, "CT%03d: query_mem by over ratio\n", tid);
	tid++;

	// Test for os_assign mem
	rc = ioctl(mckfd, IHK_OS_ASSIGN_MEM, &mem_req_nega_num);
	OKNG(rc != 0, "CT%03d: os_assign_mem by negative num_mems\n", tid);
	tid++;

	rc = ioctl(mckfd, IHK_OS_ASSIGN_MEM, &mem_req_null_mems);
	OKNG(rc != 0, "CT%03d: os_assign_mem by NULL array\n", tid);
	tid++;

	rc = ioctl(mckfd, IHK_OS_ASSIGN_MEM, &mem_req_nega_chunk_size);
	OKNG(rc != 0, "CT%03d: os_assign_mem by negative min_chunk_size\n", tid);
	tid++;

	rc = ioctl(mckfd, IHK_OS_ASSIGN_MEM, &mem_req_nega_ratio);
	OKNG(rc != 0, "CT%03d: os_assign_mem by negative ratio\n", tid);
	tid++;

	rc = ioctl(mckfd, IHK_OS_ASSIGN_MEM, &mem_req_over_ratio);
	OKNG(rc != 0, "CT%03d: os_assign_mem by over ratio\n", tid);
	tid++;

	// Test for os_release mem
	rc = ioctl(mckfd, IHK_OS_RELEASE_MEM, &mem_req_nega_num);
	OKNG(rc != 0, "CT%03d: os_release_mem by negative num_mems\n", tid);
	tid++;

	rc = ioctl(mckfd, IHK_OS_RELEASE_MEM, &mem_req_null_mems);
	OKNG(rc != 0, "CT%03d: os_release_mem by NULL array\n", tid);
	tid++;

	rc = ioctl(mckfd, IHK_OS_RELEASE_MEM, &mem_req_nega_chunk_size);
	OKNG(rc != 0, "CT%03d: os_release_mem by negative min_chunk_size\n", tid);
	tid++;

	rc = ioctl(mckfd, IHK_OS_RELEASE_MEM, &mem_req_nega_ratio);
	OKNG(rc != 0, "CT%03d: os_release_mem by negative ratio\n", tid);
	tid++;

	rc = ioctl(mckfd, IHK_OS_RELEASE_MEM, &mem_req_over_ratio);
	OKNG(rc != 0, "CT%03d: os_release_mem by over ratio\n", tid);
	tid++;

	// Test for os_query mem
	rc = ioctl(mckfd, IHK_OS_QUERY_MEM, &mem_req_nega_num);
	OKNG(rc != 0, "CT%03d: os_query_mem by negative num_mems\n", tid);
	tid++;

	rc = ioctl(mckfd, IHK_OS_QUERY_MEM, &mem_req_null_mems);
	OKNG(rc != 0, "CT%03d: os_query_mem by NULL array\n", tid);
	tid++;

	rc = ioctl(mckfd, IHK_OS_QUERY_MEM, &mem_req_nega_chunk_size);
	OKNG(rc != 0, "CT%03d: os_query_mem by negative min_chunk_size\n", tid);
	tid++;

	rc = ioctl(mckfd, IHK_OS_QUERY_MEM, &mem_req_nega_ratio);
	OKNG(rc != 0, "CT%03d: os_query_mem by negative ratio\n", tid);
	tid++;

	rc = ioctl(mckfd, IHK_OS_QUERY_MEM, &mem_req_over_ratio);
	OKNG(rc != 0, "CT%03d: os_query_mem by over ratio\n", tid);
	tid++;

	// Test for os_assign mem
	rc = ioctl(mckfd, IHK_OS_ASSIGN_MEM, &mem_req_nega_num);
	OKNG(rc != 0, "CT%03d: os_assign_mem by negative num_mems\n", tid);
	tid++;

#if 0
	// Test for release cpu
	rc = ihk_release_cpu(devidx, cpu_array_1, -1);
	OKNG(rc != 0, "CT%03d: release_cpu by negative num_cpus\n", tid);
	tid++;

	rc = ihk_release_cpu(devidx, NULL, 1);
	OKNG(rc != 0, "CT%03d: release_cpu by NULL array\n", tid);
	tid++;

	// Test for query cpu
	rc = ihk_query_cpu(devidx, cpu_array_1, -1);
	OKNG(rc != 0, "CT%03d: query_cpu by negative num_cpus\n", tid);
	tid++;

	rc = ihk_query_cpu(devidx, NULL, 1);
	OKNG(rc != 0, "CT%03d: query_cpu by NULL array\n", tid);
	tid++;

	// Test for os_assign cpu
	rc = ihk_os_assign_cpu(devidx, cpu_array_1, -1);
	OKNG(rc != 0, "CT%03d: os_assign_cpu by negative num_cpus\n", tid);
	tid++;

	rc = ihk_os_assign_cpu(devidx, NULL, 1);
	OKNG(rc != 0, "CT%03d: os_assign_cpu by NULL array\n", tid);
	tid++;

	// Test for os_relaase cpu
	rc = ihk_os_release_cpu(devidx, cpu_array_1, -1);
	OKNG(rc != 0, "CT%03d: os_release_cpu by negative num_cpus\n", tid);
	tid++;

	rc = ihk_os_release_cpu(devidx, NULL, 1);
	OKNG(rc != 0, "CT%03d: os_release_cpu by NULL array\n", tid);
	tid++;

	// Test for os_query cpu
	rc = ihk_os_query_cpu(devidx, cpu_array_1, -1);
	OKNG(rc != 0, "CT%03d: os_query_cpu by negative num_cpus\n", tid);
	tid++;

	rc = ihk_os_query_cpu(devidx, NULL, 1);
	OKNG(rc != 0, "CT%03d: os_query_cpu by NULL array\n", tid);
	tid++;

	// Test for os_set_ikc_map
	rc = ihk_os_set_ikc_map(devidx, &ikc_cpu_map, -1);
	OKNG(rc != 0, "CT%03d: os_set_ikc_map by negative num_cpus\n", tid);
	tid++;

	rc = ihk_os_set_ikc_map(devidx, NULL, 1);
	OKNG(rc != 0, "CT%03d: os_set_ikc_map by NULL array\n", tid);
	tid++;

	// Test for os_get_ikc_map
	rc = ihk_os_get_ikc_map(devidx, &ikc_cpu_map, -1);
	OKNG(rc != 0, "CT%03d: os_get_ikc_map by negative num_cpus\n", tid);
	tid++;

	rc = ihk_os_get_ikc_map(devidx, NULL, 1);
	OKNG(rc != 0, "CT%03d: os_get_ikc_map by NULL array\n", tid);
	tid++;
#endif
out:
	if (mckfd > 0) {
		close(mckfd);
	}
	return 0;
}
