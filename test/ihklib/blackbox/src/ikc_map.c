#define _GNU_SOURCE
#include <stdio.h>
#include <sched.h>

int main(int argc, char **argv)
{
	printf("%d\n", sched_getcpu());
}
