#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <torc.h>

#define N               1024 * 1024 * 8
#define NUM_NODES       12
#define NUM_THREADS     4
#define SPLIT_THRESHOLD 4

#define LOCAL_SPLIT ((N) / ((NUM_NODES) * (NUM_THREADS) * 2))

void mergesort_master(int* a, int size, int depth);
void mergesort_slave(int* a, int size);
void merge(int arr[], int l, int m, int r);
void sort(int arr[], int l, int r);
void print_array(int arr[], int size);
int verify_sort(int arr[], int size);

int cmp(const void* a, const void* b) { return (*(int*) a - *(int*)b); }

void merge(int arr[], int l, int m, int r)
{
    int i, j, k;
    int n1, n2;
    int *a, *b;

    n1 = m - l + 1;
    n2 = r - m;

    a = (int*)malloc(n1 * sizeof(int));
    b = (int*)malloc(n2 * sizeof(int));

    for (i = 0; i < n1; ++i) a[i] = arr[l + i];
    for (j = 0; j < n2; ++j) b[j] = arr[m + 1 + j];

    i = j = 0;
    k = l;

    while (i < n1 && j < n2) {
        if (a[i] <= b[j]) arr[k++] = a[i++];
        else arr[k++] = b[j++];
    }

    // Check for uneven size for A
    while (i < n1) arr[k++] = a[i++];

    // Check for uneven size for B
    while (j < n2) arr[k++] = b[j++];
    
    free(a); 
    free(b);
}

void sort(int arr[], int l, int r)
{
    int m;

    if (l < r) {
        m = l + (r - 1) / 2;

        sort(arr, l, m);
        sort(arr, m + 1, r);
        merge(arr, l, m, r);
    }
}

void mergesort_master(int* a, int size, int depth)
{
    int err;
    int n1, n2;
    int current_rank;

    if (depth == SPLIT_THRESHOLD) {
        MPI_Comm_rank(MPI_COMM_WORLD, &current_rank);
        mergesort_slave(a, size);
    } else {
        MPI_Comm_rank(MPI_COMM_WORLD, &n1);
        n2 = n1 + (1 << depth);

        torc_create(-1, mergesort_master, 3,
                    size, MPI_INT, CALL_BY_REF,
                    1, MPI_INT, CALL_BY_COP,
                    1, MPI_INT, CALL_BY_COP,
                    a, (size >> 1), depth + 1);
        
        torc_create_direct(n1, mergesort_master, 3,
                    size, MPI_INT, CALL_BY_REF,
                    1, MPI_INT, CALL_BY_COP,
                    1, MPI_INT, CALL_BY_COP,
                    (a + (size >> 1)), size - (size >> 1), depth + 1);

        torc_waitall();

        merge(a, 0, (size - 1) / 2, size - 1);

        err = verify_sort(a, size);
        if (err) {
            fprintf(stderr, "Merge error!\n");
            exit(-1);
        }

    }
}

void mergesort_slave(int* a, int size) {
    int err, current_rank;

    if (size <= LOCAL_SPLIT) {
        sort(a, 0, size - 1);
    } else {
        MPI_Comm_rank(MPI_COMM_WORLD, &current_rank);

        torc_create_direct(current_rank, mergesort_slave, 2,
                    size, MPI_INT, CALL_BY_REF,
                    1, MPI_INT, CALL_BY_COP,
                    a, (size >> 1));

        torc_create_direct(current_rank, mergesort_slave, 2,
                    size, MPI_INT, CALL_BY_REF,
                    1, MPI_INT, CALL_BY_COP,
                    (a + (size >> 1)), size - (size >> 1));

        torc_waitall();

        merge(a, 0, (size - 1) / 2, size - 1);

        err = verify_sort(a, size);
        if (err) {
            fprintf(stderr, "Merge error!\n");
            exit(-1);
        }
    }
}

int verify_sort(int* a, int size)
{
    int i;
    for (i = 1; i < size; ++i) if (a[i] > a[i - 1]) return 1;
    return 0;
}

int main(int argc, char** argv) 
{
    int i, err;
    int *arr;
    double start;

    torc_init(argc, argv, MODE_MS);

    torc_register_task(mergesort_master);
    torc_register_task(mergesort_slave);
    torc_register_task(merge);
    torc_register_task(sort);

    arr = (int*) malloc(N * sizeof(int));
    for (i = 0; i < N; ++i) arr[i] = rand() % 100000;

    start = torc_gettime();
    //for (i = 0; i < torc_num_nodes(); ++i) 
    torc_create(-1, mergesort_master, 3,
                N, MPI_INT, CALL_BY_REF,
                1, MPI_INT, CALL_BY_COP,
                1, MPI_INT, CALL_BY_COP,
                arr, N, 0);
    torc_waitall();

    printf("Mergesort for %d elements took %.4lf seconds\n", N, torc_gettime() - start);

}