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

    printf("Free p1\n");
    my_free(p1);
    debug_heap();

    printf("Free p3\n");
    my_free(p3);
    debug_heap();

    printf("Free p2\n");
    my_free(p2);
    debug_heap();

    printf("malloc 100 bytes\n");
    int *p4 = (int *) my_malloc(100);
    debug_heap();
    *p4 = 1000;
    printf("p4: %d\n\n", *p4);

    printf("realloc 150 bytes\n");
    p4 = (int *)my_realloc(p4, 150);
    debug_heap();
    printf("p4: %d\n\n", *p4);

    printf("realloc 50 bytes\n");
    p4 = (int *)my_realloc(p4, 50);
    debug_heap();
    printf("p4: %d\n\n", *p4);

    printf("realloc 300 bytes\n");
    p4 = (int *)my_realloc(p4, 300);
    debug_heap();
    printf("p4: %d\n\n", *p4);
    
    printf("Calloc 10 * int");
    int *p5 = (int *)my_calloc(10, sizeof(int));
    debug_heap();
    for(size_t i=0; i<10; i++){
        printf("%d ", p5[i]);
    }
    printf("\n");

    return 0;
}