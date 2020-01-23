#define _GNU_SOURCE	 /* See feature_test_macros(7) */
#include <unistd.h>
#include <sys/syscall.h>   /* For SYS_xxx definitions */

int main(int argc, char **argv)
{
	syscall(2001);
	return 123;
}
