#include "MemoryAllocator.h"
#include <stddef.h> // size_t, offsetof
#include <unistd.h> // sbrk, brk
#include <stdio.h> // debug purposes

// implementation based on document below
// https://sourceware.org/glibc/wiki/MallocInternals

// compatible for both 32-bit 64-bit systems
#define QWORD 8
#define QWORD_BIT 3
#define MIN_CHUNK_SIZE 32
#define RELEASE_THRESHOLD (128 * 1024) // 128KB
// padding for 32/64-bit systems
#define ALIGN(x) (((((x) - 1) >> QWORD_BIT) << QWORD_BIT) + QWORD) // -O0 compilers maybe?
#define SIZE(mchunkptr) (((mchunkptr)->size) & ~1)
#define PREV_INUSE(mchunkptr) (((mchunkptr)->size) & 1)

static mchunk *base = NULL;

static void remove_node(mchunk *node){
    mchunk* rchunk = (mchunk*)((char *)node + SIZE(node));
    rchunk->size = rchunk->size | 1; // prev in use = 1

    mchunk* fwd = node->fwd;
    mchunk* bck = node->bck;

    if(bck)
        bck->fwd = fwd;
    else
        base = fwd;
    
    if(fwd)
        fwd->bck = bck;
}

static void insert_node(mchunk *node){
    mchunk* rchunk = (mchunk*)((char *)node + SIZE(node));
    rchunk->size = rchunk->size & ~1; // prev in use = 1

    mchunk *fwd = base;
    node->fwd = fwd;
    node->bck = NULL;

    if(fwd)
        fwd->bck = node;
    
    base = node;
}

static void *extend_heap(size_t size){
    size_t *new_chunk = (size_t *) sbrk(size);
    if(new_chunk == (size_t *)-1){ // no more heap left
        return NULL;
    }

    mchunk *mchunkptr = (mchunk *)((char *)new_chunk - 2 * QWORD);
    mchunkptr->size = size | PREV_INUSE(mchunkptr); // success prev in use
    size_t *prev_size = (size_t *)((char *)mchunkptr + size);
    *prev_size = size;
    insert_node(mchunkptr);

    mchunk *epilogue_hdr = (mchunk *)((char *)mchunkptr + size + QWORD);
    epilogue_hdr->size = 0 | 0; // size = 0, prev in-use = 0
    return mchunkptr;
}

static void *find_chunk(size_t size){
    mchunk *curr = base;
    while(curr){
        if(SIZE(curr) && SIZE(curr) >= size){ // Free & Enough size
            break;
        }
        curr = curr->fwd;
    }
    return curr;
}

static void split_chunk(mchunk *mchunkptr, size_t size){ // free_ptr is the same as data_ptr for non-free blocks
    if(mchunkptr->size < size + MIN_CHUNK_SIZE)
        return;

    mchunk *new_mchunkptr = (mchunk *)((char *)mchunkptr + size);
    new_mchunkptr->size = mchunkptr->size - size; // total size - split size, prev in use = 0
    new_mchunkptr->prev_size = size;

    // [original chunk] [new chunk] | [next chunk]
    mchunk *next_mchunkptr = (mchunk *)((char *)new_mchunkptr + new_mchunkptr->size);
    next_mchunkptr->prev_size = new_mchunkptr->size;

    // original chunk
    mchunkptr->size = size | PREV_INUSE(mchunkptr);

    insert_node(new_mchunkptr);
}

static mchunk* fuse_chunk(mchunk *mchunkptr){
    if((mchunkptr->prev_size & ~1) && !PREV_INUSE(mchunkptr)){ // not prologue header && prev in use = 0
        mchunk *lchunk = (mchunk *)((char *)mchunkptr - mchunkptr->prev_size);
        lchunk->size += mchunkptr->size;
        size_t *prev_size = (size_t *)((char *)lchunk + lchunk->size);
        *prev_size = SIZE(lchunk);
        remove_node(mchunkptr);
        mchunkptr = lchunk;
    }


    mchunk* rchunk = (mchunk *)((char *)mchunkptr + SIZE(mchunkptr));
    if(!SIZE(rchunk)){ // epilogue hdr
        return mchunkptr;
    }

    mchunk* rrchunk = (mchunk *)((char *)rchunk + SIZE(rchunk)); // well prev_in_use in rchunk is already 0
    if(PREV_INUSE(rrchunk)){ // rchunk in use
        return mchunkptr;
    }

    mchunkptr->size += rchunk->size; // rchunk prev in use = 0
    size_t *prev_size = (size_t *)((char *)mchunkptr + SIZE(mchunkptr));
    *prev_size = SIZE(mchunkptr);
    remove_node(rchunk);
    return mchunkptr;
}

/*
static void copy_block(meta_block *source, meta_block *dest){
    size_t *source_data, *dest_data;
    source_data = (size_t *) source->data;
    dest_data = (size_t *) dest->data;
    size_t copy_len = (source->size < dest->size ? source->size : dest->size) / WORD;
    for(size_t i = 0; i  < copy_len; i++){
        dest_data[i] = source_data[i];
    }
}
*/

/*
static int is_valid_addr(void* p){
    if(base){ // is the heap initialized?
        if(p < sbrk(0)){ // is the address between the start and break?
            return 1;
        }
    }
    return 0;
}
*/

static int init = 0;

void static my_malloc_init(){
    size_t *prologue_hdr = (size_t *)sbrk(8);
    if(prologue_hdr == (size_t *)-1)
        return;
    *prologue_hdr = 0 | 1; // size = 0, prev in use = 1
    size_t *epilogue_hdr = (size_t *)sbrk(8);
    if(epilogue_hdr == (size_t *)-1)
        return;
    *epilogue_hdr = 0 | 1; // size = 0, prev in use = 1
    init = 1;
}

void *my_malloc(size_t size){
    if(!init)
        my_malloc_init();

    size_t aligned_size = ALIGN(size + QWORD);
    size_t chunk_size = aligned_size > MIN_CHUNK_SIZE ? aligned_size : MIN_CHUNK_SIZE;

    // base = NULL > return NULL
    // no suitable free chunk > return NULL
    // suitable free chunk > return mchunk*
    mchunk *mchunkptr = find_chunk(chunk_size);

    if(!mchunkptr){
        mchunkptr = extend_heap(chunk_size);
        mchunkptr = fuse_chunk(mchunkptr);
        remove_node(mchunkptr);
        split_chunk(mchunkptr, chunk_size);
        return mchunkptr->payload;
    }

    // found suitable free chunk
    remove_node(mchunkptr);
    split_chunk(mchunkptr, chunk_size);
    return mchunkptr->payload;
}

void *my_calloc(size_t number, size_t size){
    size_t total_size = ALIGN(number * size);
    size_t *new = my_malloc(total_size);
    size_t clear_len = total_size >> QWORD_BIT;
    for(size_t i = 0; i < clear_len; i++){
        new[i] = 0;
    }
    return new;
}

void *my_realloc(void *ptr, size_t size){
    /*
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
        */
}


void my_free(void* data_ptr){
    /*
    if(is_valid_addr(data_ptr)){
        size_t *header_ptr = (size_t *) data_ptr - 1;
        *header_ptr = *header_ptr & ~1; // set NOT FREE

        if()

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
        */
}

// for debugging

void debug_heap(){
    mchunk *curr = base;
    printf("heap top: %p\n", sbrk(0));
    printf("\n[HEAP START]\n");

    if(!curr){
        printf("Heap is Empty\n");
        printf("[HEAP END]\n\n");
        return;
    }

    while(curr){
        printf("mchunkptr [%p] | size: %ld | prev in use: %ld | fwd: %p | bck: %p\n", curr, curr->size & ~1, curr->size & 1, curr->fwd, curr->bck);

        curr = curr->fwd;
    }

    printf("[HEAP END]\n\n");
}