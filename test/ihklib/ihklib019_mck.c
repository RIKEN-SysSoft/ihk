#define _GNU_SOURCE             /* See feature_test_macros(7) */
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/time.h>

int main(int argc, char** argv) {
  int nproc, rank, ierr;
  struct timeval tv;

  gettimeofday(&tv, NULL);
  printf("Before-MPI_Init %ld.%ld\n", tv.tv_sec, tv.tv_usec);

  MPI_Init(&argc, &argv);
  MPI_Comm_size(MPI_COMM_WORLD, &nproc);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  if (rank == 0) {
	  syscall(900); /* It makes McKernel hang */
	  //fprintf(stderr, "hang\n");
  }

  MPI_Finalize();

  gettimeofday(&tv, NULL);
  printf("After-MPI_Finalize %ld.%ld\n", tv.tv_sec, tv.tv_usec);

  return 0;
}
