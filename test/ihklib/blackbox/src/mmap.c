#define _GNU_SOURCE
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#define MAP_HUGE_2MB    (21 << MAP_HUGE_SHIFT)
#define MAX_NUM_CHUNKS (1UL<<10)

enum ihk_os_pgsize {
	IHK_OS_PGSIZE_4KB,
	IHK_OS_PGSIZE_64KB,
	IHK_OS_PGSIZE_2MB,
	IHK_OS_PGSIZE_32MB,
	IHK_OS_PGSIZE_1GB,
	IHK_OS_PGSIZE_16GB,
	IHK_OS_PGSIZE_512MB,
	IHK_OS_PGSIZE_4TB,
	IHK_MAX_NUM_PGSIZES
};

int main(int argc, char **argv)
{
	int ret;
	int i;
	int opt;
	int fd = -1;
	char **mem[IHK_MAX_NUM_PGSIZES] = { 0 };
	int message;
	int fd_in = -1, fd_out = -1;
	int num_pages = 0;
	int kernel_mode = 0;
	size_t kernel_mem_size = 0;
	intptr_t kernel_addr = 0;
	size_t user_mem_size = 0;
	int mmap_flags = MAP_PRIVATE;
	int page_size = PAGE_SIZE;
	enum ihk_os_pgsize page_size_index = IHK_OS_PGSIZE_64KB;

	fd_in = open(argv[1], O_RDWR);
	if (fd_in == -1) {
		int errno_save = errno;

		printf("%s: open returned %d\n", __FILE__, errno);
		ret = -errno_save;
		goto out;
	}

	fd_out = open(argv[2], O_RDWR);
	if (fd_out == -1) {
		int errno_save = errno;

		printf("%s: open returned %d\n", __FILE__, errno);
		ret = -errno_save;
		goto out;
	}

	while ((opt = getopt(argc, argv, "p:u:f:k:")) != -1) {
		switch (opt) {
		case 'p': /* specify page size */
			page_size_index = atoi(optarg);
			break;
		case 'u': /* size of memory to allocate */
			user_mem_size = atoi(optarg);
			break;
		case 'f': /* speficy filename for file mapping */
			fd = open(optarg, O_RDWR);
			if (fd == -1) {
				int errno_save = errno;

				printf("%s: open returned %d\n",
						__FILE__, errno);
				ret = -errno_save;
				goto out;
			}
			break;
		case 'k': /* allocate memory in kernel space */
			kernel_mode = 1;
			kernel_mem_size = atoi(optarg);
			break;
		default:
			printf("unknown option %c\n", optopt);
			ret = -EINVAL;
			goto out;
		}
	}

	if (fd == -1) {
		mmap_flags |= MAP_ANONYMOUS;
	}

	switch (page_size_index) {
	case IHK_OS_PGSIZE_2MB:
		mmap_flags |= MAP_HUGETLB | MAP_HUGE_2MB;
		page_size = 1UL << 21;
		break;
	default:
		break;
	}

	if (!user_mem_size) {
		user_mem_size = page_size * MAX_NUM_CHUNKS;
	}
	num_pages = user_mem_size / page_size;

	mem[page_size_index] = malloc(sizeof(char *) * num_pages);
	if (!mem[page_size_index]) {
		int errno_save = -errno;

		printf("%s: malloc returned %d\n", __FILE__, errno);
		ret = errno_save;
		goto sync_out;
	}

	/* Let parent take stat */
	ret = write(fd_out, &message, sizeof(int));
	if (ret != sizeof(int)) {
		int errno_save = errno;

		printf("%s: write returned %d, errno: %d\n",
		       __FILE__, ret, errno);
		ret = ret >= 0 ? ret : -errno_save;
		goto sync_out;
	}

	/* Wait until parent takes reference stat */
	ret = read(fd_in, &message, sizeof(int));
	if (ret != sizeof(int)) {
		int errno_save = errno;

		printf("%s: read returned %d, errno: %d\n",
		       __FILE__, ret, errno);
		ret = ret >= 0 ? ret : -errno_save;
		goto sync_out;
	}

	printf("[ INFO ] num_pages: %d, page_size: %d\n",
	       num_pages, page_size);
	for (i = 0; i < num_pages; i++) {
		mem[page_size_index][i] = mmap(0, page_size,
					       PROT_READ | PROT_WRITE,
					       mmap_flags,
					       fd,
					       fd == -1 ? 0 : i * page_size
					       );
		if (mem[page_size_index][i] == MAP_FAILED) {
			ret = -errno;
			goto sync_out;
		}

		memset(mem[page_size_index][i], 0xff, page_size);
	}

	if (kernel_mode) {
		kernel_addr = syscall(2003, kernel_mem_size);
		if (kernel_addr == -1) {
			int errno_save = -errno;

			printf("%s: syscall 2003 failed: %s\n",
					__FILE__, strerror(errno_save));
			ret = errno_save;
			goto sync_out;
		}
	}

 sync_out:

	/* Let parent take stat */
	ret = write(fd_out, &message, sizeof(int));
	if (ret != sizeof(int)) {
		int errno_save = errno;

		printf("%s: write returned %d, errno: %d\n",
		       __FILE__, ret, errno);
		ret = ret >= 0 ? ret : -errno_save;
		goto out;
	}

	/* Wait until parent takes usage stat */
	ret = read(fd_in, &message, sizeof(int));
	if (ret != sizeof(int)) {
		int errno_save = errno;

		printf("%s: read returned %d, errno: %d\n",
		       __FILE__, ret, errno);
		ret = ret >= 0 ? ret : -errno_save;
		goto out;
	}

	ret = 0;
 out:
	if (fd != -1) {
		close(fd);
	}
	if (fd_in != -1) {
		close(fd_in);
	}
	if (fd_out != -1) {
		close(fd_out);
	}
	for (i = 0; i < num_pages; i++) {
		munmap(mem[page_size_index][i], page_size);
	}
	if (mem[page_size_index]) {
		free(mem[page_size_index]);
	}
	if (kernel_mode) {
		syscall(2004, kernel_addr);
	}
	return ret;
}
