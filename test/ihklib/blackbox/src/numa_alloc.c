#define _GNU_SOURCE
#include <numa.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <ctype.h>

struct pernode_mem {
	int node_id;
	unsigned long memory_size; /* in bytes */
};

static int parse_unit(char *args, int *node_id, unsigned long *memory_size)
{
	int ret = 0;
	char *ps = strdup(args);

	if (!ps) {
		ret = -errno;
		printf("%s: strdup failed\n", __func__);
		goto out;
	}

	char *p = ps;
	char *temp_p = NULL;
	int node_temp = -1;
	unsigned long unit;

	while (*p != ':' && *p != '\0') {
		p++;
	}

	if (*p == ':') {
		*p = '\0';
	}
	else {
		ret = -EINVAL;
		printf("%s: invalid format (<node>:<memsize[unit]>)\n",
			__func__);
		goto out;
	}

	node_temp = atoi(ps);

	temp_p = ++p;
	while (isdigit(*p) && *p != '\0') {
		p++;
	}

	switch (toupper(*p)) {
	case 'K':
		unit = 1024;
		break;
	case 'M':
		unit = 1024 * 1024;
		break;
	case 'G':
		unit = 1024 * 1024 * 1024;
		break;
	case '\0':
		unit = 1;
		break;
	default:
		ret = -EINVAL;
		printf("%s: invalid format (<node>:<memsize[unit]>\n",
			__func__);
		goto out;
	}
	*p = '\0';

	*node_id = node_temp;
	*memory_size = atoi(temp_p) * unit;
out:
	free(ps);
	return ret;
}

static int parse_args(char *args, struct pernode_mem **nodes)
{
	int ret = 0, i = 0, count = 1;
	char *_args = NULL;
	char *c = NULL;
	char *tok;

	_args = strdup(args);
	if (!_args) {
		int errno_save = -errno;

		printf("%s: strdup returned %d\n", __func__, errno);
		ret = errno_save;
		goto out;
	}

	c = _args;
	while (*c != '\0') {
		if (*c == ',' && *(c + 1) != '\0') {
			count++;
		}
		c++;
	}

	*nodes = malloc(sizeof(struct pernode_mem) * count);
	if (!(*nodes)) {
		int errno_save = -errno;

		printf("%s: malloc returned %d\n", __func__, errno);
		ret = errno_save;
		goto out;
	}

	tok = strtok(_args, ",");
	while (tok) {
		int node_id = 0;
		unsigned long memory_size = 0;

		ret = parse_unit(tok, &node_id, &memory_size);
		if (ret) {
			printf("%s: parse unit returned %d\n", __func__, ret);
			goto out;
		}
		printf("node ID: %d  memory size: %lu\n", node_id, memory_size);

		(*nodes)[i].node_id = node_id;
		(*nodes)[i++].memory_size = memory_size;
		tok = strtok(NULL, ",");
	}

	ret = count;
out:
	free(_args);
	return ret;
}

int main(int argc, char **argv)
{
	int ret, i, opt;
	int message = 1;
	int fd_in = -1, fd_out = -1;
	int num_nodes = 1;
	struct pernode_mem *nodes = NULL;
	char **node_addrs = NULL;

	ret = numa_available();
	if (ret == -1) {
		printf("%s: libnuma is unavailable\n", __FILE__);
		goto out;
	}

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

	while ((opt = getopt(argc, argv, "n:")) != -1) {
		switch (opt) {
		case 'n':
			ret = parse_args(optarg, &nodes);
			if (ret <= 0) {
				printf("%s: parse args failed with %d\n",
					__FILE__, ret);
				goto out;
			}
			num_nodes = ret;
			node_addrs = malloc(sizeof(char *) * num_nodes);
			if (!node_addrs) {
				ret = -errno;
				printf("%s: malloc failed with %d\n",
					__FILE__, ret);
				goto out;
			}
			break;
		default:
			printf("unknown option %c\n", optopt);
			exit(1);
		}
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

	for (i = 0; i < num_nodes; i++) {
		node_addrs[i] = numa_alloc_onnode(nodes[i].memory_size,
				nodes[i].node_id);
		if (node_addrs[i] == NULL) {
			ret = -errno;
			printf("%s: numa_alloc_onnode(%lu, %d) failed with %d",
				__FILE__, nodes[i].memory_size,
				nodes[i].node_id, ret);
			goto out;
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
	for (i = 0; i < num_nodes; i++) {
		if (node_addrs[i]) {
			numa_free(node_addrs[i], nodes[i].memory_size);
		}
	}
	if (nodes) {
		free(nodes);
	}
	return ret;
}
