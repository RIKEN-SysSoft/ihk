#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
	unsigned long va;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s (address)\n", argv[0]);
		return 1;
	}

	va = strtoul(argv[1], NULL, 16);
	
	printf("Address = %016lx\n", va);
	printf("     L4 = %03ld (%03lx) => %03lx\n", (va >> 39) & 511, 
	       (va >> 39) & 511, ((va >> 39) & 511) * 8);
	printf("     L3 = %03ld (%03lx) => %03lx\n", (va >> 30) & 511, 
	       (va >> 30) & 511, ((va >> 30) & 511) * 8);
	printf("     L2 = %03ld (%03lx) => %03lx\n", (va >> 21) & 511, 
	       (va >> 21) & 511, ((va >> 21) & 511) * 8);
	printf("     L1 = %03ld (%03lx) => %03lx\n", (va >> 12) & 511, 
	       (va >> 12) & 511, ((va >> 12) & 511) * 8);
	
	return 0;
}
