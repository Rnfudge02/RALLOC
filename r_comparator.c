#define _POSIX_C_SOURCE 200809L

//Standard library includes
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "r_alloc.h"

//Test information
#define NUM_TESTS 10
#define NUM_ITERATIONS 10000

//Sizes to use for testing
size_t test_sizes[NUM_TESTS] = {16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192};

//Benchmark function
void benchmark(void* (*alloc_func)(size_t), void (*free_func)(void*), double *results) {
    //Complete NUM_TESTS times
    for (int i = 0; i < NUM_TESTS; i++) {
        //Set up testing information, and get start time
        size_t size = test_sizes[i];
        struct timespec start, end;
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &start);
        
        //Run desired freeing function
        for (int j = 0; j < NUM_ITERATIONS; j++) {
            void *ptr = alloc_func(size);
            free_func(ptr);
        }
        
        //Get end time, calculate duration and store in array
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &end);
        double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
        results[i] = elapsed;
    }
}

//Main function
int main() {
    double r_times[NUM_TESTS], libc_times[NUM_TESTS];

    //Benchmark both alloc and free functions
    benchmark(r_malloc, r_free, r_times);
    benchmark(malloc, free, libc_times);
    
    //Write the results to a file
    FILE *fp = fopen("results.csv", "w");
    fprintf(fp, "Size,r_malloc,malloc\n");

    //Write row to file
    for (int i = 0; i < NUM_TESTS; i++) {
        fprintf(fp, "%zu,%f,%f\n", test_sizes[i], r_times[i], libc_times[i]);
    }

    //Close the file pointer and return 0 to caller
    fclose(fp);
    return 0;
}
