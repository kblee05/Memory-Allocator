#include "MemoryAllocator.h"
#include <stddef.h> // size_t, offsetof
#include <unistd.h> // sbrk, brk

// compatible for both 32-bit 64-bit systems
#define ALIGN_UNIT sizeof(size_t)

// padding for 32/64-bit systems
// needs optimizing by bit operators
#define ALIGN(x) ((((x)-1)/ALIGN_UNIT)*ALIGN_UNIT+ALIGN_UNIT)

#define META_SIZE offsetof(meta_block, data)

static meta_block *base = NULL;

static meta_block *find_meta(meta_block **last, size_t size){

}

static meta_block *extend_heap(size_t size){

}

static void split_block(meta_block *b, size_t size){

}

static void fuse_block(meta_block *b){

}

static void copy_block(meta_block *source, meta_block *dest){

}

void *my_malloc(size_t size){

}

void *my_calloc(size_t number, size_t size){

}

void *my_realloc(void *ptr, size_t size){

}

void free(void* ptr){
    
}
