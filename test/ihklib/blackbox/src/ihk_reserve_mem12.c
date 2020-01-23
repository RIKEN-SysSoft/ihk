#include <errno.h>
#include <ihklib.h>
#include <numaif.h>
#include <numa.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "util.h"
#include "okng.h"
#include "mem.h"
#include "params.h"
#include "linux.h"

const char param[] =
	"total with different "
	"available size variation";
const char *values[] = {
	"no variation",
	"30% taken from a node",
};

int main(int argc, char **argv)
{
	int ret;
	int i;
	int excess;

	params_getopt(argc, argv);

	struct mems mems_ref = { 0 };

	ret = mems_ls(&mems_ref, "MemFree", 1.0);
	INTERR(ret, "mems_ls returned %d\n", ret);

	excess = mems_ref.num_mem_chunks - 4;
	if (excess > 0) {
		ret = mems_shift(&mems_ref, excess);
		INTERR(ret, "mems_shift returned %d\n", ret);
	}

	struct mems mems_input[2] = { 0 };
	double mem_taken_ratio[] = { 0, 0.3 };

	int ret_expected[2] = { 0, -ENOMEM };

	/* Precondition */
	ret = linux_insmod(0);
	INTERR(ret, "linux_insmod returned %d\n", ret);

	int allowed_var = 10;

	ret = ihk_reserve_mem_conf(0, IHK_RESERVE_MEM_TOTAL,
				   &allowed_var);
	INTERR(ret, "ihk_reserve_mem_conf returned %d\n",
	       ret);

	/* Activate and check */
	for (i = 0; i < 2; i++) {
		int j;
		unsigned long len;
		void *mem_taken;

		START("test-case: %s: %s\n", param, values[i]);

		system("pushd /sys/fs/cgroup/memory/user.slice; "
		       "printf '[ INFO ] memory.memsw.limit_in_bytes: ';"
		       "cat memory.memsw.limit_in_bytes; "
		       "printf '[ INFO ] memory.limit_in_bytes: ';"
		       "cat memory.limit_in_bytes");

		/* Create variance */
		if (i == 1) {
			int id;
			struct bitmask *nodemask = numa_allocate_nodemask();
			long lret;

			id = mems_ref.mem_chunks[0].numa_node_number;
			len = ((long)(mems_ref.mem_chunks[0].size *
				      mem_taken_ratio[i]) &
			       ~((1UL << 16) - 1));

			numa_bitmask_setbit(nodemask, id);
			INFO("len: %ld (%lx, %ld MiB), "
			     "nodemask: %lx, maxnode: %ld\n",
			     len, len, len >> 20,
			     *nodemask->maskp, nodemask->size);

#define MPOL_F_STATIC_NODES     (1 << 15)

			lret = set_mempolicy(MPOL_BIND | MPOL_F_STATIC_NODES,
				     nodemask->maskp, nodemask->size);
			INTERR(lret, "set_mempolicy returned %d\n", errno);

			numa_free_nodemask(nodemask);

			mem_taken = mmap(0, len,
					 PROT_READ | PROT_WRITE,
					 MAP_ANONYMOUS | MAP_PRIVATE,
					 -1, 0);
			INTERR(mem_taken == MAP_FAILED,
			       "mmap returned %d\n", errno);
			memset(mem_taken, 0, len);
		}

		for (j = 0; j < mems_ref.num_mem_chunks; j++) {
			char cmd[4096];
			int id = mems_ref.mem_chunks[j].numa_node_number;

			sprintf(cmd,
				"awk 'BEGIN { ORS=\"\" }"
				"/MemFree/ { "
				"printf $0; "
				"print \" (\"; print $4 / 1024; print \" MiB)\\n\";"
				" }' /sys/devices/system/node/node%d/meminfo",
				id);
			system(cmd);
		}

		ret = mems_ls(&mems_input[i], "MemFree", 0.95);
		INTERR(ret, "mems_ls returned %d\n", ret);

		excess = mems_input[i].num_mem_chunks - 4;
		if (excess > 0) {
			ret = mems_shift(&mems_input[i], excess);
			INTERR(ret, "mems_shift returned %d\n", ret);
		}

		ret = ihk_reserve_mem(0, mems_input[i].mem_chunks,
				      mems_input[i].num_mem_chunks);
		OKNG(ret == ret_expected[i],
		     "return value: %d, expected: %d\n",
		     ret, ret_expected[i]);

		if (i == 0) {
			ret = mems_check_var(allowed_var / (double)100);
			OKNG(ret == 0,
			     "NUMA-node variation of reserved size\n");
		}

		/* Clean up */
		ret = mems_release();
		INTERR(ret, "mems_release returned %d\n", ret);

		if (i == 1) {
			munmap(mem_taken, len);
		}
	}
	ret = 0;
 out:
	linux_rmmod(0);
	return ret;
}

