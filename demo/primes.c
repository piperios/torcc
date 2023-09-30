#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <torc.h>
#include <sys/wait.h>
#include <unistd.h>

#define UPTO 1000000

void primes(long *ni, int *res) {
	int i, flag = 0;
	long n = *ni;

	for (i = 2; i <= (int)sqrt(n) ; ++i) {
    if (n % i == 0) {
      flag = 1;
      break;
    }
  }

	*res = (flag == 0) ? 1 : 0;	
}

int main(int argc, char **argv)
{
    int cnt = UPTO;
    int *result;
    int i;
		long di;
    double t0, t1;

    torc_register_task(primes);

    printf("address(slave)=%p\n", primes);
    torc_init(argc, argv, MODE_MS);

    result = (int *)malloc(cnt*sizeof(int));

    torc_enable_stealing();
    torc_enable_prefetching();

    t0 = torc_gettime();
    result[0] = result[1] = 0;
    for (i=2; i<cnt; i++) {
				di = i;
        result[i] = 0;
        torc_task(-1, primes, 2,
                     1, MPI_LONG, CALL_BY_COP,
                     1, MPI_INT, CALL_BY_RES,
                     &di, &result[i]);
    }

    torc_waitall();
    t1 = torc_gettime();

    //for (i = 0; i < cnt; i++) {
    //    printf("Received: is_prime(%d)=%s\n", (i+1), (result[i]) ? "true" : "false");
    //}

    printf("Elapsed time: %.2lf seconds\n", t1-t0);
    torc_finalize();
    return 0;
	
	return 0;
}
