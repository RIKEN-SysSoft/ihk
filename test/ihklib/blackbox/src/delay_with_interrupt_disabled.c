#define _GNU_SOURCE	 /* See feature_test_macros(7) */
#include <unistd.h>
#include <sys/syscall.h>   /* For SYS_xxx definitions */

int main(int argc, char **argv)
{
	syscall(2003);
	return 0;
}
