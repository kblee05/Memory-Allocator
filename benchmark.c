#include <stdio.h>
#include <time.h>
#include <stdlib.h>

#ifdef USE_STD_MALLOC
    #define TARGET_MALLOC malloc
    #define TARGET_FREE free
    #define TARGET_REALLOC realloc
    #define TARGET_CALLOC calloc
    const char* ALLOC_NAME = "Standard glibc malloc";
#else
    #include "MemoryAllocator.h"
    #define TARGET_MALLOC my_malloc
    #define TARGET_FREE my_free
    #define TARGET_REALLOC my_realloc
    #define TARGET_CALLOC my_calloc
    const char* ALLOC_NAME = "My Custom malloc";
#endif

#define ITERATIONS 10000
#define MAX_ALLOC_SIZE 4096

int main() {
    void *ptrs[ITERATIONS];
    clock_t start, end;

    printf("=== Benchmark Target: %s ===\n", ALLOC_NAME);

    start = clock();

    for (int i = 0; i < ITERATIONS; i++) {
        size_t size = (rand() % MAX_ALLOC_SIZE) + 1;
        ptrs[i] = TARGET_MALLOC(size);
        if (!ptrs[i]) {
            printf("Allocation failed\n");
            return 1;
        }
    }

    for (int i = 0; i < ITERATIONS; i++) {
        TARGET_FREE(ptrs[i]);
    }

    end = clock();
    
    double time_taken = ((double)(end - start)) / CLOCKS_PER_SEC;
    double ops = (ITERATIONS * 2.0) / time_taken;

    printf("Time: %f sec | Throughput: %.0f ops/sec\n\n", time_taken, ops);
    return 0;
}