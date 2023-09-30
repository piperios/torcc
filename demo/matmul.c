/* Serial program for matrix-matrix product.
 *
 * VVD
 */

#include <stdio.h>
#include <stdlib.h>
#include <torc.h>

#define N       1024
#define TASKS   M * M
#define WORKERS 4

#define Afile "Amat1024"
#define Bfile "Bmat1024"
#define Cfile "CmatRes1024"

int **A, **B, **C;
int *A_data, *B_data, *C_data;
int **res, *res_data;

int readmat(char *fname, int *mat, int n); 
int writemat(char *fname, int *mat, int n);

void task_exec(int worker_id, int *results);
void* thread_worker(void* arg);

int M, S;
int ntasks, taskid = 0;

void node_allocate(int m, int s, int* A_in, int* B_in)
{
	int i;
	M = m;
	S = s;

	A_data = (int *) malloc(N * N * sizeof(int));
	B_data = (int *) malloc(N * N * sizeof(int));

	A = (int **) malloc(N * sizeof(int*));
	B = (int **) malloc(N * sizeof(int*));

	for (i = 0; i < N; i++) {
		A[i] = (int*) &A_data[N * i];
		B[i] = (int*) &B_data[N * i];
	}

	for (i = 0; i < N * N; i++) {
		A_data[i] = A_in[i];
		B_data[i] = B_in[i];
	}

	res_data = (int*) malloc(S * S * sizeof(int));
	res = (int**) malloc(S * sizeof(int*));

	return;
}

void node_deallocate() 
{
	free(A[0]);
	free(A);
	free(B[0]);
	free(B);

	free(res[0]);
	free(res);
}

void task_exec(int worker_id, int* results) 
{
	int i, j, k, x, y, outer_lim, inner_lim;
	int r_i = 0, sum = 0;

	x = worker_id / M;
	y = worker_id % M;

	outer_lim = (x + 1) * S;
	inner_lim = (y + 1) * S;

	for (i = x * S; i < outer_lim; ++i) {
		for (j = y * S; j < outer_lim; ++j) {
			for (k = 0, sum = 0; k < N; ++k) {
				sum += A[i][k] * B[k][j];
			}
			results[r_i++] = sum;
		}
	}
}

int main(int argc, char** argv)
{
	int i, task_num, **master_res;
	double start, avg_time = 0.0;

	A_data = (int*) malloc(N * N * sizeof(int));
	B_data = (int*) malloc(N * N * sizeof(int));
	C_data = (int*) malloc(N * N * sizeof(int));

	A = (int**) malloc(N * sizeof(int*));
	B = (int**) malloc(N * sizeof(int*));
	C = (int**) malloc(N * sizeof(int*));

	for (i = 0; i < N; i++) {
		A[i] = (int*) &A_data[N * i];
		B[i] = (int*) &B_data[N * i];
		C[i] = (int*) &C_data[N * i];
	}

	if (readmat("Amat1024", (int *) A_data, N) < 0)
		exit (1 + printf("file problem \n"));
	if (readmat("Bmat1024", (int *) B_data, N) < 0)
		exit (1 + printf("file problem \n"));


	S = 32; // Check 64 and 256
	M = N / S;
	ntasks = TASKS;

	torc_init(argc, argv, MODE_MS);

    torc_register_task(node_allocate);
    torc_register_task(node_deallocate);
    torc_register_task(task_exec);

	master_res = (int**) malloc(ntasks * sizeof(int*));
	for (i = 0; i < ntasks; ++i) master_res[i] = (int*) malloc(S * S * sizeof(int));

	for (i = 1; i < torc_num_nodes(); i++) {
		torc_create_direct(i, node_allocate, 4,
                     1, MPI_INT, CALL_BY_COP,
                     1, MPI_INT, CALL_BY_COP,
                     N * N, MPI_INT, CALL_BY_REF,
                     N * N, MPI_INT, CALL_BY_REF,
                     &M, &S, A_data, B_data);
	}
	torc_waitall();

	start = torc_gettime();

	task_num = taskid;
	while (task_num < ntasks) {
		torc_create_direct(i, task_exec, 2,
                     1, MPI_INT, CALL_BY_COP,
                     S * S, MPI_INT, CALL_BY_RES,
                     task_num, master_res[task_num]);
		task_num = ++taskid;

	}
	torc_waitall();

	avg_time = torc_gettime() - start;
	
	for (i = 1; i < torc_num_nodes(); i++) {
		torc_create_direct(i, node_deallocate, 0);
	}

	printf("[S = %d] Average time: %lf\n", S, avg_time);

	writemat(Cfile, (int *) C, N);

	for (i = 0; i < ntasks; ++i) free(master_res[i]);
	free(master_res);

	free(A[0]);
	free(A);
	free(B[0]);
	free(B);
	free(C[0]);
	free(C);

	torc_waitall();

	return 0;
}

#define _mat(i,j) (mat[(i)*n + (j)])

int readmat(char *fname, int *mat, int n)
{
	FILE *fp;
	int  i, j;
	
	if ((fp = fopen(fname, "r")) == NULL)
		return (-1);
	for (i = 0; i < n; i++)
		for (j = 0; j < n; j++)
			if (fscanf(fp, "%d", &_mat(i,j)) == EOF)
			{
				fclose(fp);
				return (-1); 
			};
	fclose(fp);
	return (0);
}


int writemat(char *fname, int *mat, int n)
{
	FILE *fp;
	int  i, j;
	
	if ((fp = fopen(fname, "w")) == NULL)
		return (-1);
	for (i = 0; i < n; i++, fprintf(fp, "\n"))
		for (j = 0; j < n; j++)
			fprintf(fp, " %d", _mat(i, j));
	fclose(fp);
	return (0);
}
