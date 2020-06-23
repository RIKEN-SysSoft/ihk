#define _GNU_SOURCE	 /* See feature_test_macros(7) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/mman.h>
#include "util.h"
#include "okng.h"
#include "mem.h"


int mems_init(struct mems *mems, int num_mem_chunks)
{
	int ret;

	if (num_mem_chunks == 0) {
		ret = 0;
		goto out;
	}

	mems->mem_chunks = mmap(0,
				sizeof(struct ihk_mem_chunk) * num_mem_chunks,
			  PROT_READ | PROT_WRITE,
			  MAP_ANONYMOUS | MAP_PRIVATE,
			  -1, 0);
	if (mems->mem_chunks == MAP_FAILED) {
		ret = -errno;
		goto out;
	}

	mems->num_mem_chunks = num_mem_chunks;
	ret = 0;
 out:
	return ret;
}

int mems_copy(struct mems *dst, struct mems *src)
{
	int ret;

	if (dst->mem_chunks) {
		dst->mem_chunks = mremap(dst->mem_chunks,
					 sizeof(struct ihk_mem_chunk) *
					 dst->num_mem_chunks,
					 sizeof(struct ihk_mem_chunk) *
					 src->num_mem_chunks,
					 MREMAP_MAYMOVE);
		dst->num_mem_chunks = src->num_mem_chunks;
	} else {
		ret = mems_init(dst, src->num_mem_chunks);
		if (ret) {
			goto out;
		}
	}

	memcpy(dst->mem_chunks, src->mem_chunks,
	       sizeof(struct ihk_mem_chunk) * src->num_mem_chunks);

	ret = 0;
 out:
	return ret;
}

/* type: "MemTotal" or "MemFree" */
int _mems_ls(struct mems *mems, char *type, double ratio, long constant)
{
	int ret;
	FILE *fp = NULL;

	if (mems->mem_chunks == NULL) {
		ret = mems_init(mems, MAX_NUM_MEM_CHUNKS);
		if (ret != 0) {
			goto out;
		}
	}

	char cmd[4096];
	unsigned long memfree;
	int numa_node_number = 0;
	char keyword[1024];

	if (strcmp(type, "MemTotal") == 0) {
		sprintf(keyword, "%s", "present");
	} else if (strcmp(type, "MemFree") == 0) {
		sprintf(keyword, "%s", "nr_free_pages");
	} else {
		ret = -EINVAL;
		goto out;
	}

	sprintf(cmd,
		"awk -v keyword=\"%s\" -f %s/bin/zoneinfo.awk /proc/zoneinfo",
		keyword, QUOTE(CMAKE_INSTALL_PREFIX));

	fp = popen(cmd, "r");
	if (fp == NULL) {
		ret = -errno;
		goto out;
	}

	while (fscanf(fp, "id: %d, nr_free_pages: %ld\n",
		      &numa_node_number, &memfree) == 2) {
#if 0
		printf("%s: id: %d, %s: # of pages: %ld, "
		       "size: %ld (%ld MiB)\n",
		       __func__, numa_node_number, keyword,
		       memfree, memfree * PAGE_SIZE,
		       (memfree * PAGE_SIZE) >> 20);
#endif
		memfree *= PAGE_SIZE;

#define RESERVE_MEM_GRANULE (1024UL * 1024 * 4)
		mems->mem_chunks[numa_node_number].size +=
			((unsigned long)(memfree * ratio) &
			 ~(RESERVE_MEM_GRANULE - 1));
		mems->mem_chunks[numa_node_number].numa_node_number =
			numa_node_number;
	}
	pclose(fp);
	fp = NULL;

	mems->mem_chunks = mremap(mems->mem_chunks,
				  sizeof(struct ihk_mem_chunk) *
				  mems->num_mem_chunks,
				  sizeof(struct ihk_mem_chunk) *
				  numa_node_number + 1,
				  MREMAP_MAYMOVE);
	if (mems->mem_chunks == MAP_FAILED) {
		int errno_save = errno;

		printf("%s: mremap returned %d\n", __func__, errno);
		ret = -errno_save;
		goto out;
	}

	mems->num_mem_chunks = numa_node_number + 1;
	INFO("# of NUMA nodes: %d\n", mems->num_mem_chunks);

	if (constant != -1) {
		mems_fill(mems, constant);
	}

	ret = 0;
 out:
	if (fp) {
		pclose(fp);
	}

	return ret;
}

int mems_ls(struct mems *mems)
{
	return _mems_ls(mems, "MemFree", 0.9, 1UL << 30);
}

int mems_free(struct mems *mems)
{
	int ret;
	FILE *fp = NULL;
	int max_numa_node_number = -1;

	if (mems->mem_chunks == NULL) {
		ret = mems_init(mems, MAX_NUM_MEM_CHUNKS);
		if (ret != 0) {
			goto out;
		}
	}

	fp = popen("dmesg | awk ' /NUMA.*free mem/ { size[$4] = $10; if ($4 > max) { max = $4 } } END { for (i = 0; i <= max; i++) { if (size[i] > 0) { printf \"%d %d\\n\", i, size[i] } } }'",
		   "r");
	if (fp == NULL) {
		ret = -errno;
		goto out;
	}

	do {
		unsigned long memfree;
		int numa_node_number;

		ret = fscanf(fp, "%d %ld\n", &numa_node_number, &memfree);

		if (ret == -1)
			break;

		if (ret != 2) {
			continue;
		}

		if (numa_node_number > max_numa_node_number) {
			max_numa_node_number = numa_node_number;
		}

		printf("%s: %d: %ld (%ld MiB)\n",
		       __func__, numa_node_number, memfree, memfree >> 20);

		mems->mem_chunks[numa_node_number].size =
			(memfree & ~(RESERVE_MEM_GRANULE - 1));
		mems->mem_chunks[numa_node_number].numa_node_number =
			numa_node_number;
	} while (ret);

	pclose(fp);
	fp = NULL;

	mems->mem_chunks = mremap(mems->mem_chunks,
				  sizeof(struct ihk_mem_chunk) *
				  mems->num_mem_chunks,
				  sizeof(struct ihk_mem_chunk) *
				  max_numa_node_number + 1,
				  MREMAP_MAYMOVE);
	if (mems->mem_chunks == MAP_FAILED) {
		int errno_save = errno;

		printf("%s: mremap returned %d\n", __func__, errno);
		ret = -errno_save;
		goto out;
	}

	mems->num_mem_chunks = max_numa_node_number + 1;
	ret = 0;
 out:
	if (fp) {
		pclose(fp);
	}

	return ret;
}

int mems_push(struct mems *mems, unsigned long size, int numa_node_number)
{
	int ret;

	if (mems->mem_chunks == NULL) {
		ret = mems_init(mems, 1);
		if (ret != 0) {
			goto out;
		}
		mems->num_mem_chunks = 0;
	} else {
		mems->mem_chunks = mremap(mems->mem_chunks,
					  sizeof(struct ihk_mem_chunk) *
					  mems->num_mem_chunks,
					  sizeof(struct ihk_mem_chunk) *
					  (mems->num_mem_chunks + 1),
					  MREMAP_MAYMOVE);
		if (mems->mem_chunks == MAP_FAILED) {
			ret = -errno;
			goto out;
		}
	}

	mems->mem_chunks[mems->num_mem_chunks].size = size;
	mems->mem_chunks[mems->num_mem_chunks].numa_node_number =
		numa_node_number;
	mems->num_mem_chunks++;
	ret = 0;
 out:
	return ret;
}

int mems_pop(struct mems *mems, int n)
{
	int ret;

	if (n == 0) {
		ret = 0;
		goto out;
	}

	if (mems->num_mem_chunks < n || mems->mem_chunks == NULL) {
		ret = 1;
		goto out;
	}

	if (mems->num_mem_chunks == n) {
		ret = munmap(mems->mem_chunks,
			     sizeof(struct ihk_mem_chunk) *
			     mems->num_mem_chunks);
		if (ret) {
			ret = -errno;
			goto out;
		}
		mems->mem_chunks = NULL;
		mems->num_mem_chunks = 0;
		ret = 0;
		goto out;
	}

	mems->mem_chunks = mremap(mems->mem_chunks,
				  sizeof(struct ihk_mem_chunk) *
				  mems->num_mem_chunks,
				  sizeof(struct ihk_mem_chunk) *
				  (mems->num_mem_chunks - n),
				  MREMAP_MAYMOVE);
	if (mems->mem_chunks == MAP_FAILED) {
		ret = -errno;
		goto out;
	}

	mems->num_mem_chunks -= n;

	ret = 0;
 out:
	return ret;
}

int mems_shift(struct mems *mems, int n)
{
	int ret;

	if (n == 0) {
		ret = 0;
		goto out;
	}

	if (mems->num_mem_chunks < n || mems->mem_chunks == NULL) {
		ret = 1;
		goto out;
	}

	if (mems->num_mem_chunks == n) {
		ret = munmap(mems->mem_chunks,
			     sizeof(struct ihk_mem_chunk) *
			     mems->num_mem_chunks);
		if (ret) {
			ret = -errno;
			goto out;
		}
		mems->mem_chunks = NULL;
		mems->num_mem_chunks = 0;
		ret = 0;
		goto out;
	}

	memmove(mems->mem_chunks, mems->mem_chunks + n,
		sizeof(struct ihk_mem_chunk) * (mems->num_mem_chunks - n));

	mems->mem_chunks = mremap(mems->mem_chunks,
				  sizeof(struct ihk_mem_chunk) *
				  mems->num_mem_chunks,
				  sizeof(struct ihk_mem_chunk) *
				  (mems->num_mem_chunks - n),
				  MREMAP_MAYMOVE);
	if (mems->mem_chunks == MAP_FAILED) {
		ret = -errno;
		goto out;
	}

	mems->num_mem_chunks -= n;

	ret = 0;
 out:
	return ret;
}

void mems_fill(struct mems *mems, unsigned long size)
{
	int i;

	for (i = 0; i < mems->num_mem_chunks; i++) {
		mems->mem_chunks[i].size = size;
	}
}

void mems_multiply(struct mems *mems, double ratio)
{
	int i;

	for (i = 0; i < mems->num_mem_chunks; i++) {
		mems->mem_chunks[i].size *= ratio;
	}
}

void mems_dump(struct mems *mems)
{
	int i;

	if (mems->mem_chunks == NULL) {
		INFO("mems->mem_chunks is NULL\n");
		return;
	}

	for (i = 0; i < mems->num_mem_chunks; i++) {
		char size_str[256];

		if (mems->mem_chunks[i].size == -1) {
			sprintf(size_str, "all");
		} else {
			sprintf(size_str, "%ld (%ld MiB)",
				mems->mem_chunks[i].size,
				mems->mem_chunks[i].size >> 20);
		}

		INFO("mem_chunks[%d]: size: %s, numa_node_number: %d\n",
		     i, size_str, mems->mem_chunks[i].numa_node_number);
	}
}

static void mems_sum(struct mems *mems, long *sum)
{
	int i;

	memset(sum, 0, sizeof(long) * MAX_NUM_MEM_CHUNKS);

	for (i = 0; i < mems->num_mem_chunks; i++) {
		sum[mems->mem_chunks[i].numa_node_number] +=
			mems->mem_chunks[i].size;
	}
}

void mems_dump_sum(struct mems *mems)
{
	int i;
	long sum[MAX_NUM_MEM_CHUNKS] = { 0 };
	int found;

	if (mems) {
		mems_sum(mems, sum);
	}

	found = 0;
	for (i = 0; i < MAX_NUM_MEM_CHUNKS; i++) {
		if (sum[i]) {
			INFO("size: %ld MiB, numa_node_number: %d\n",
			     sum[i] >> 20, i);
			found = 1;
		}
	}

	if (!found) {
		INFO("(none)\n");
	}
}

int mems_compare(struct mems *result, struct mems *expected,
		 struct mems *margin)
{
	int i;
	long sum_result[MAX_NUM_MEM_CHUNKS] = { 0 };
	long sum_expected[MAX_NUM_MEM_CHUNKS] = { 0 };
	long sum_margin[MAX_NUM_MEM_CHUNKS] = { 0 };

	if (result == NULL && expected == NULL) {
		return 0;
	}

	mems_sum(result, sum_result);
	mems_sum(expected, sum_expected);
	if (margin) {
		mems_sum(margin, sum_margin);
	}

	for (i = 0; i < MAX_NUM_MEM_CHUNKS; i++) {
		if (sum_result[i] > 0) {
			INFO("numa: %d, actual: %ld MiB, "
			     "expected: %ld MiB, margin: %ld MiB\n",
			     i,
			     sum_result[i] >> 20,
			     sum_expected[i] >> 20,
			     sum_margin[i] >> 20);
		}

		if (sum_result[i] < sum_expected[i] ||
		    sum_result[i] > sum_expected[i] +
		    sum_margin[i]) {
			return 1;
		}
	}

	return 0;
}

int mems_check_reserved(struct mems *expected, struct mems *margin)
{
	int ret;
	int num_mem_chunks;
	struct mems mems = { 0 };

	ret = ihk_get_num_reserved_mem_chunks(0);
	INTERR(ret < 0, "ihk_get_num_reserved_mem_chunks returned %d\n",
	       ret);

	num_mem_chunks = ret;

	if (num_mem_chunks > 0) {
		ret = mems_init(&mems, num_mem_chunks);
		INTERR(ret,
		       "mems_init returned %d, num_mem_chunks: %d\n",
		       ret, num_mem_chunks);

		ret = ihk_query_mem(0, mems.mem_chunks, mems.num_mem_chunks);
		INTERR(ret, "ihk_query_cpu returned %d\n",
		       ret);
	}

	ret = mems_compare(&mems, expected, margin);

 out:
	return ret;
}

int mems_check_var(double allowed_var)
{
	int ret;
	int i;
	int num_mem_chunks;
	struct mems mems = { 0 };
	long node_size[MAX_NUM_MEM_CHUNKS] = { 0 };
	long min = LONG_MAX, max = LONG_MIN, sum = 0;
	int nnodes = 0;

	ret = ihk_get_num_reserved_mem_chunks(0);
	INTERR(ret < 0, "ihk_get_num_reserved_mem_chunks returned %d\n",
	       ret);

	num_mem_chunks = ret;

	if (num_mem_chunks > 0) {
		ret = mems_init(&mems, num_mem_chunks);
		INTERR(ret,
		       "mems_init returned %d, num_mem_chunks: %d\n",
		       ret, num_mem_chunks);

		ret = ihk_query_mem(0, mems.mem_chunks, mems.num_mem_chunks);
		INTERR(ret, "ihk_query_cpu returned %d\n",
		       ret);
	}

	mems_sum(&mems, node_size);

	for (i = 0; i < MAX_NUM_MEM_CHUNKS; i++) {
		if (node_size[i] == 0) {
			continue;
		}

		if (node_size[i] < min) {
			min = node_size[i];
		}
		if (node_size[i] > max) {
			max = node_size[i];
		}
		sum += node_size[i];
		nnodes++;
	}

	ret = 0;
	for (i = 0; i < MAX_NUM_MEM_CHUNKS; i++) {
		double var;

		if (node_size[i] == 0) {
			continue;
		}

		var = node_size[i] / (sum / (double)nnodes);
		INFO("id: %d, size: %ld (%ld MiB), size/ave: %1.2f\n",
		     i, node_size[i], node_size[i] >> 20, var);
		if (var < 1 - allowed_var ||
		    var > 1 + allowed_var) {
			ret = 1;
		}
	}
 out:
	return ret;
}

/* ratio is represented by percentage */
int mems_check_ratio(struct mems *divisor, struct mems *ratios,
		     double *ratios_out)
{
	int ret;
	int num_mem_chunks;
	struct mems dividend = { 0 };
	int i;
	long sum_dividend[MAX_NUM_MEM_CHUNKS] = { 0 };
	long sum_divisor[MAX_NUM_MEM_CHUNKS] = { 0 };
	long sum_ratios[MAX_NUM_MEM_CHUNKS] = { 0 };
	int fail = 0;

	ret = ihk_get_num_reserved_mem_chunks(0);
	INTERR(ret < 0, "ihk_get_num_reserved_mem_chunks returned %d\n",
	       ret);

	num_mem_chunks = ret;

	if (num_mem_chunks > 0) {
		ret = mems_init(&dividend, num_mem_chunks);
		INTERR(ret,
		       "mems_init returned %d, num_mem_chunks: %d\n",
		       ret, num_mem_chunks);

		ret = ihk_query_mem(0, dividend.mem_chunks,
				    dividend.num_mem_chunks);
		INTERR(ret, "ihk_query_cpu returned %d\n",
		       ret);
	}

	mems_sum(&dividend, sum_dividend);
	mems_sum(divisor, sum_divisor);
	mems_sum(ratios, sum_ratios);

	for (i = 0; i < MAX_NUM_MEM_CHUNKS; i++) {
		if (sum_divisor[i]) {
			ratios_out[i] = sum_dividend[i] /
				(double)sum_divisor[i];
			double limit = sum_ratios[i] /
				(double)100;

			if (ratios_out[i] < limit) {
				fail = 1;
			}

			INFO("numa_node_number %d: "
			     "%ld (%ld MiB) / %ld (%ld MiB) = %1.4f, "
			     "lower limit %1.4f\n",
			     i,
			     sum_dividend[i],
			     sum_dividend[i] >> 20,
			     sum_divisor[i],
			     sum_divisor[i] >> 20,
			     ratios_out[i], limit);
		}
	}

	ret = 0;
 out:
	return ret ? ret : fail;
}

int mems_check_total(unsigned long lower_limit)
{
	int ret;
	int i;
	int num_mem_chunks;
	struct mems mems;
	long sums[MAX_NUM_MEM_CHUNKS] = { 0 };
	long sum = 0;

	ret = ihk_get_num_reserved_mem_chunks(0);
	INTERR(ret < 0, "ihk_get_num_reserved_mem_chunks returned %d\n",
	       ret);

	num_mem_chunks = ret;

	if (num_mem_chunks > 0) {
		ret = mems_init(&mems, num_mem_chunks);
		INTERR(ret,
		       "mems_init returned %d, num_mem_chunks: %d\n",
		       ret, num_mem_chunks);

		ret = ihk_query_mem(0, mems.mem_chunks, mems.num_mem_chunks);
		INTERR(ret, "ihk_query_cpu returned %d\n",
		       ret);

		mems_sum(&mems, sums);

		for (i = 0; i < MAX_NUM_MEM_CHUNKS; i++) {
			sum += sums[i];
		}
	}
	INFO("total: %ld, lower limit: %ld\n", sum, lower_limit);

	if (sum < lower_limit) {
		ret = 1;
		goto out;
	}

	ret = 0;
 out:
	return ret;
}

int _mems_reserve(int nlinux, double ratio, unsigned long constant)
{
	int ret;
	struct mems mems = { 0 };
	int excess;

	ret = _mems_ls(&mems, "MemFree", ratio, constant);
	INTERR(ret, "mems_ls returned %d\n", ret);

	excess = mems.num_mem_chunks - nlinux;
	if (excess > 0) {
		ret = mems_shift(&mems, excess);
		INTERR(ret, "mems_shift returned %d\n", ret);
	}

	ret = ihk_reserve_mem(0, mems.mem_chunks,
			      mems.num_mem_chunks);
	INTERR(ret, "ihk_reserve_mem returned %d\n", ret);

	ret = 0;
 out:
	return ret;
}

int mems_reserve(void)
{
	return _mems_reserve(4, 0.9, 1UL << 30);
}

int mems_release(void)
{
	int ret;
	struct mems mems;

	ret = ihk_get_num_reserved_mem_chunks(0);
	INTERR(ret < 0, "ihk_get_num_reserved_mem_chunks returned %d\n",
	       ret);

	if (ret == 0) {
		goto out;
	}

	ret = mems_init(&mems, ret);
	INTERR(ret, "mems_init returned %d\n", ret);

	ret = ihk_query_mem(0, mems.mem_chunks, mems.num_mem_chunks);
	INTERR(ret, "ihk_query_mem returned %d\n", ret);

	ret = ihk_release_mem(0, mems.mem_chunks, mems.num_mem_chunks);
	INTERR(ret, "ihk_release_mem returned %d\n", ret);

	ret = 0;
 out:
	return ret;
}

int mems_reserved(struct mems *mems)
{
	int ret;

	ret = ihk_get_num_reserved_mem_chunks(0);
	INTERR(ret < 0, "ihk_get_num_reserved_mem_chunks returned %d\n",
	       ret);

	if (ret == 0) {
		goto out;
	}

	ret = mems_init(mems, ret);
	INTERR(ret, "mems_init returned %d\n", ret);

	ret = ihk_query_mem(0, mems->mem_chunks, mems->num_mem_chunks);
	INTERR(ret, "ihk_query_mem returned %d\n", ret);

	ret = 0;
 out:
	return ret;
}

int mems_check_assigned(struct mems *expected, struct mems *margin)
{
	int ret;
	struct mems mems = { 0 };

	ret = ihk_os_get_num_assigned_mem_chunks(0);
	INTERR(ret < 0, "ihk_get_num_assigned_mem_chunks returned %d\n",
	       ret);

	if (ret > 0) {
		ret = mems_init(&mems, ret);
		INTERR(ret, "mems_init returned %d\n", ret);

		ret = ihk_os_query_mem(0, mems.mem_chunks, mems.num_mem_chunks);
		INTERR(ret, "ihk_query_mem returned %d\n", ret);
	}

	ret = mems_compare(&mems, expected, margin);

 out:
	return ret;
}

int mems_os_assign(void)
{
	int ret;
	struct mems mems;

	ret = mems_reserved(&mems);
	INTERR(ret, "mems_reserved returned %d\n", ret);

	ret = ihk_os_assign_mem(0, mems.mem_chunks,
				mems.num_mem_chunks);
	INTERR(ret, "ihk_os_assign_mem returned %d\n", ret);

	ret = 0;
out:
	return ret;
}

int mems_os_release(void)
{
	int ret;
	struct mems mems;

	ret = ihk_os_get_num_assigned_mem_chunks(0);
	INTERR(ret < 0, "ihk_os_get_num_assigned_mem_chunks returned %d\n",
	       ret);

	if (ret == 0) {
		goto out;
	}

	ret = mems_init(&mems, ret);
	INTERR(ret, "mems_init returned %d\n", ret);

	ret = ihk_os_query_mem(0, mems.mem_chunks, mems.num_mem_chunks);
	INTERR(ret, "ihk_os_query_mem returned %d\n", ret);

	ret = ihk_os_release_mem(0, mems.mem_chunks, mems.num_mem_chunks);
	INTERR(ret, "ihk_os_release_mem returned %d\n", ret);

	ret = 0;
 out:
	return ret;
}
