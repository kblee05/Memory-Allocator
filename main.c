#include "MemoryAllocator.h"
#include <stdio.h>

int main(int argc, char* argv[]){
    printf("Malloc 100 bytes\n");
    int *p1 = (int *) my_malloc(100);
    debug_heap();

    printf("Malloc 50 bytes\n");
    int *p2 = (int *) my_malloc(50);
    debug_heap();

    printf("Malloc 4 bytes\n");
    int *p3 = (int *) my_malloc(4);
    debug_heap();
    /*
    printf("Free p1\n");
    my_free(p1);
    debug_heap();
    
    printf("Malloc 20 bytes\n");
    int *p3 = (int *) my_malloc(20);
    debug_heap();

    printf("Free p2 & p3\n");
    my_free(p2);
    debug_heap();
    my_free(p3);
    debug_heap();

    printf("Realloc 10 to 200 bytes\n");
    int *p4 = (int *) my_malloc(10);
    debug_heap();
    p4 = (int *) my_realloc(p4, 200);
    debug_heap();

    printf("Malloc garbage values\n");
    int *p5 = (int *) my_malloc(10 * sizeof(int));
    printf("[%p]: ", (void *) p5);
    for(size_t i =0; i<10; i++){
        p5[i] = 9999;
        printf("%d ", p5[i]);
    }
    printf("\n");
    debug_heap();

    printf("Free garbage value pointer\n");
    my_free(p5);
    debug_heap();

    printf("Calloc at the same spot\n");
    int *p6 = (int *) my_calloc(10, sizeof(int));
    printf("[%p]: ", (void *) p6);
    for(size_t i=0; i<10; i++){
        printf("%d ", p6[i]);
    }
    printf("\n");
    debug_heap();
    */
    return 0;
}