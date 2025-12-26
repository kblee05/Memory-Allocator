#ifndef MEMORYALLOCATOR_H
#define MEMORYALLOCATOR_H

#include <unistd.h>

typedef struct meta_block meta_block;

struct meta_block{
    size_t size;
    meta_block *next;
    meta_block *prev;
    size_t free; // Cannot determine whether the compiler will pad "int free"
    void *ptr;
    char data[1]; // Start address of DATA_BLOCK
};

void *my_malloc(size_t size);
void *my_calloc(size_t number, size_t size);
void *my_realloc(void *ptr, size_t size);
void my_free(void* ptr);
void debug_heap();

#endif