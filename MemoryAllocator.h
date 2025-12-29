#ifndef MEMORYALLOCATOR_H
#define MEMORYALLOCATOR_H

#include <unistd.h>

// https://sourceware.org/glibc/wiki/MallocInternals

// explicit free list
// FREE Block : [Header - 8 Bytes] [Next pointer - 8 Bytes] [Prev pointer -8 Bytes] [Empty data ... ] [Footer - 8 Bytes]
// ALLOCATED Block : [Header - 8 Bytes] [User Data.. ] [Footer - 8 Bytes]
// Header & Footer : Use last bit for checking FREE

typedef struct mchunk mchunk;

struct mchunk{
    size_t prev_size;
    size_t hdr;

    union{
        struct{
            struct mchunk* fwd;
            struct mchunk* bck;
        };
        char payload[1];
    };
};

void *my_malloc(size_t size);
void *my_calloc(size_t number, size_t size);
void *my_realloc(void *ptr, size_t size);
void my_free(void* ptr);
void debug_heap();

#endif