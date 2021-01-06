/*
 * To compile:
 *   cc -Wall -g -o besteffort besteffort.c -lihk
 */
#include <ihklib.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>

static const int devidx = 0;
#define DEFAULT_MEMSZ		(2UL * 1024 * 1024 * 1024)
#define NUMA_NODES		4
#define NUMA_NODE_OFFSET	4

int
main(int argc, char *argv[])
{
	struct ihk_mem_chunk mcs[NUMA_NODES];
	unsigned long memsz;
	int val;
	int ret;
	int i;

	if (argc < 2)
		memsz = DEFAULT_MEMSZ;
	else
		memsz = strtoul(argv[1], NULL, 0);

	val = 1;
	if ((ret = ihk_reserve_mem_conf(devidx, IHK_RESERVE_MEM_BALANCED_ENABLE, &val)) < 0)
		errx(1, "ihk_reserve_mem_conf(IHK_RESERVE_MEM_BALANCED_ENABLE): %d.", ret);
	val = 1;
	if ((ret = ihk_reserve_mem_conf(devidx, IHK_RESERVE_MEM_BALANCED_BEST_EFFORT, &val)) < 0)
		errx(1, "ihk_reserve_mem_conf(IHK_RESERVE_MEM_BALANCED_BEST_EFFORT): %d.", ret);
	val = 1000;
	if ((ret = ihk_reserve_mem_conf(devidx, IHK_RESERVE_MEM_BALANCED_VARIANCE_LIMIT, &val)) < 0)
		errx(1, "ihk_reserve_mem_conf(IHK_RESERVE_MEM_BALANCED_VARIANCE_LIMIT): %d.", ret);

	printf("Reserving %lu bytes.\n", memsz);
	
	for (i = 0; i < NUMA_NODES; i++) {
		mcs[i].numa_node_number = i + NUMA_NODE_OFFSET;
		mcs[i].size = memsz / NUMA_NODES;
	}
	if ((ret = ihk_reserve_mem(devidx, mcs, NUMA_NODES)) < 0)
		errx(1, "ihk_reserve_mem(): %d.", ret);

	printf("Reserved.\n");

	return 0;
}
