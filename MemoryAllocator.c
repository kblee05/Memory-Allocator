#include "MemoryAllocator.h"
#include <stddef.h> // size_t, offsetof
#include <unistd.h> // sbrk, brk
#include <stdio.h> // debug purposes

// compatible for both 32-bit 64-bit systems
#define WORD sizeof(size_t)
#define WORD_SHIFT (sizeof(size_t) == 8 ? 3 : 2)
// padding for 32/64-bit systems
#define ALIGN(x) (((((x) - 1) >> WORD_SHIFT) << WORD_SHIFT) + WORD) // -O0 compilers maybe?
#define META_SIZE offsetof(meta_block, data)
#define RELEASE_THRESHOLD (128 * 1024) // 128KB

static meta_block *base = NULL;

static meta_block *find_block(meta_block **last, size_t size){
    meta_block *block = base;
    while(block && (block->size < size || block->free == 0)){
        *last = block;
        block = block->next;
    }
    return block;
}

static void *extend_heap(meta_block* last, size_t size){
    meta_block* block = (meta_block *) sbrk(META_SIZE + size);
    if(block == (void*)-1){
        return NULL; // break point of heap
    }
    block->size = size;
    block->prev = last;
    block->next = NULL;
    block->free = 0;
    block->ptr = (void *) block->data;
    if(!last){ // base = NULL
        base = block;
    }
    else{
        last->next = block;
    }
    return block->data;
}

static void split_block(meta_block *block, size_t size){
    if(block->size < size + META_SIZE + WORD)
        return;
    meta_block* new; // b(a+b) > b(a) -> new(b)
    new = (meta_block *) (block->data + size);
    new->size = block->size - size - META_SIZE;
    new->next = block->next;
    new->prev = block;
    new->free = 1;
    new->ptr = new->data;
    if(new->next)
        new->next->prev = new;
    block->next = new;
    block->size = size;
    block->free = 0;
}

static meta_block *fuse_block(meta_block *b){
    if(b->next && b->next->free){ // fuse next block
        b->size += b->next->size + META_SIZE;
        b->next = b->next->next;
        if(b->next){
            b->next->prev = b;
        }
    }
    return b;
}

static void copy_block(meta_block *source, meta_block *dest){
    size_t *source_data, *dest_data;
    source_data = (size_t *) source->data;
    dest_data = (size_t *) dest->data;
    size_t copy_len = (source->size < dest->size ? source->size : dest->size) / WORD;
    for(size_t i = 0; i  < copy_len; i++){
        dest_data[i] = source_data[i];
    }
}

static meta_block *get_block(void *p){
    char *temp = (char *) p;
    return (meta_block *) (temp - META_SIZE);
}

static int is_valid_addr(void* p){
    if(base){ // is the heap initialized?
        if(p > (void *) base && p < sbrk(0)){ // is the address between the start and break?
            return p == get_block(p)->ptr;
        }
    }
    return 0;
}

void *my_malloc(size_t size){
    size = ALIGN(size);

    if(!base)
        return extend_heap(base, size);

    // base is initialized
    meta_block *last;
    meta_block *b = find_block(&last, size);

    if(b){ // found a block
        split_block(b, size);
        return b->data;
    }
    // didn't found a block / base = NULL
    return extend_heap(last, size);
}

void *my_calloc(size_t number, size_t size){
    size_t total_size = ALIGN(number * size);
    size_t *new = my_malloc(total_size);
    size_t clear_len = total_size >> WORD_SHIFT;
    for(size_t i = 0; i < clear_len; i++){
        new[i] = 0;
    }
    return new;
}

void *my_realloc(void *ptr, size_t size){
    if(!is_valid_addr(ptr))
        return NULL;

    meta_block *block = get_block(ptr);
    if(size == block->size) return block->data;
    else if(size < block->size){
        if(block->size >= size + META_SIZE + WORD){
            split_block(block, size);
            return ptr; // block->data
        }
        return ptr;
    }
    else{ // size > b
        if(block->next && block->next->free &&
            size <= block->size + block->next->size + META_SIZE){
            fuse_block(block);
            split_block(block, size);
            return ptr; // block->data
        }
        else{
            void *new_ptr = my_malloc(size);
            if(!ptr) // no heap left
                return NULL;
            meta_block *new_block = get_block(new_ptr);
            copy_block(block, new_block);
            my_free(ptr);
            return new_ptr; // new_bloc->data
        }
    }
}

void my_free(void* ptr){
    if(is_valid_addr(ptr)){
        meta_block *block = get_block(ptr);
        block->free = 1;
        if(block->prev && block->prev->free){
            block = fuse_block(block->prev);
        }

        if(block->next && block->next->free){
            fuse_block(block);
        }

        if(block->next == NULL){ // currently last block of heap
            if(block->size < RELEASE_THRESHOLD) // 128 * 1024, 128KB
                return;
            
            if(block->prev){
                block->prev->next = NULL;
            }
            else{
                base = NULL;
            }
            
            brk(block);
        }
    }
}

// for debugging

#include <stdio.h>

void debug_heap(){
    meta_block *curr = base;
    printf("\n[HEAP START]\n");

    if(!curr){
        printf("Heap is Empty\n");
        printf("[HEAP END]\n\n");
        return;
    }

    int i = 0;
    while(curr){
        printf("Block %d [%p] | Size: %zu, | Free: %ld | Next: %p | Prev: %p | Data: %p | Ptr: %p\n",
                i, (void *) curr, curr->size, curr->free, 
                (void *) curr->next, (void *) curr->prev, (void *) curr->data, (void *) curr->ptr);
        
        if(curr->next && curr->next->prev != curr){
            printf("\tERROR: Heap link broken between block %d and next\n", i);
        }

        curr = curr->next;
        i++;
    }
    printf("[HEAP END]\n\n");
}