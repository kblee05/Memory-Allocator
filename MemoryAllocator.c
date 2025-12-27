#include "MemoryAllocator.h"
#include <stddef.h> // size_t, offsetof
#include <unistd.h> // sbrk, brk
#include <stdio.h> // debug purposes

// compatible for both 32-bit 64-bit systems
#define WORD sizeof(size_t)
#define WORD_SHIFT (sizeof(size_t) == 8 ? 3 : 2)
// padding for 32/64-bit systems
#define ALIGN(x) (((((x) - 1) >> WORD_SHIFT) << WORD_SHIFT) + WORD) // -O0 compilers maybe?
#define RELEASE_THRESHOLD (128 * 1024) // 128KB
// HEADER + FOOTER + NEW + PREV
#define MIN_CHUNK_SIZE (2 * sizeof(size_t) + 2 * sizeof(void *))
#define GET(p) (*(size_t *)p)
#define PUT(p, val) (*(size_t *)(p) = (val))
#define PACK(size, free) ((size) | (free))
#define GET_SIZE(p) (GET(p) & ~1)
#define IS_FREE(p) (GET(p) & 1)
#define HEADER(payload) ((size_t *)((char *)(payload) - (WORD)))
#define FOOTER(payload) ((size_t *)((char *)(payload) + GET_SIZE(HEADER(payload)) - 2 * sizeof(size_t)))
#define RCHUNK(payload) ((void *)((char *)(payload) + GET_SIZE(HEADER(payload))))
#define LCHUNK(payload) ((void *)((char *)(payload) - GET_SIZE(((char *)(payload) - 2 * sizeof(size_t)))))
#define NEXT_FREE(p) ((void **)(p))
#define PREV_FREE(p) ((void **)((size_t *)p + 1))
#define SET_NEXT_FREE(p, ptr) (*NEXT_FREE(p) = (ptr))
#define SET_PREV_FREE(p, ptr) (*PREV_FREE(p) = (ptr))

static void *base = NULL;

static void remove_node(void *node){
    void *next = *NEXT_FREE(node);
    void *prev = *PREV_FREE(node);
    if(!prev){ // base == node
        base = next;
    }
    else{
        SET_NEXT_FREE(prev, next);
    }
    
    if(next){
        SET_PREV_FREE(next, prev);
    }
}

static void insert_node(void *node){
    SET_NEXT_FREE(node, base);
    SET_PREV_FREE(node, NULL);
    if(base){
        SET_PREV_FREE(base, node);
    }
    base = node;
}

static void *extend_heap(size_t chunk_size){
    size_t *payload = (size_t *) sbrk(chunk_size); // payload + header + footer
    if(payload == (size_t *)-1){ // no more heap left
        return NULL;
    }
    PUT(HEADER(payload), chunk_size | 1); // replace epilogue header as new header
    PUT(FOOTER(payload), chunk_size | 1); // set free initially
    PUT(HEADER(RCHUNK(payload)), 0); // epilogue header
    return payload;
}

static void *find_payload(size_t chunk_size){
    size_t *curr = base;
    while(curr){
        if(IS_FREE(HEADER(curr)) && *HEADER(curr) >= chunk_size){ // Free
            break;
        }
        curr = *NEXT_FREE(curr);
    }
    return curr;
}

static void split_chunk(void *payload, size_t chunk_size){ // free_ptr is the same as data_ptr for non-free blocks
    if(GET_SIZE(payload) < chunk_size + MIN_CHUNK_SIZE)
        return;
    
    size_t *header = HEADER(payload);
    size_t prev_size = GET_SIZE(header);
    *header = chunk_size & ~1; // set size and mark not free
    *FOOTER(payload) = chunk_size | 0; // set footer

    void *new_payload = (void *)((char *)payload + chunk_size);
    size_t new_size = prev_size - chunk_size;
    PUT(HEADER(new_payload), new_size | 1); // set leftover size and set free
    PUT(FOOTER(new_payload), new_size | 1);
    base = new_payload;

    remove_node(payload);
    insert_node(new_payload);
}

static void* fuse_chunk(void *curr){
    void *rchunk = *(void **)RCHUNK(curr);
    if(!rchunk) // no chunk at right
        return curr;

    if(!IS_FREE(HEADER(rchunk))){
        return curr;
    }

    size_t total_size = GET_SIZE(HEADER(curr)) + GET_SIZE(HEADER(rchunk));
    PUT(HEADER(curr), total_size | 1);
    PUT(FOOTER(curr), total_size | 1);

    remove_node(rchunk);
    return curr;
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

static int is_valid_addr(void* p){
    if(base){ // is the heap initialized?
        if(p >  base && p < sbrk(0)){ // is the address between the start and break?
            return 1;
        }
    }
    return 0;
}

void *my_malloc(size_t size){
    size_t aligned_size = ALIGN(size);
    size_t chunk_size = aligned_size > MIN_CHUNK_SIZE ? aligned_size : MIN_CHUNK_SIZE;
    void *payload;
    if(!base){
        void *heap_start = sbrk(sizeof(size_t));
        if(heap_start == (void *)-1)
            return NULL;
        PUT(heap_start, 0);

        payload = extend_heap(chunk_size);
        PUT(HEADER(payload), chunk_size | 0); // replace epilogue header as new header
        PUT(FOOTER(payload), chunk_size | 0); // set not free
        return payload;
    }

    // base is initialized
    void *free_payload = find_payload(chunk_size);

    if(free_payload){ // found a block
        split_chunk(free_payload, chunk_size);
        payload = free_payload;
    }
    // didn't found a block / base = NULL
    payload = extend_heap(chunk_size);
    PUT(HEADER(payload), chunk_size | 0); // replace epilogue header as new header
    PUT(FOOTER(payload), chunk_size | 0); // set not free
    return payload;
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
    void *curr = base;
    printf("\n[HEAP START]\n");

    if(!curr){
        printf("Heap is Empty\n");
        printf("[HEAP END]\n\n");
        return;
    }

    while(curr){
        printf("Header [%p] | Payload: %p | Size: %zu, | Free: %ld | Next: %p | Prev: %p\n",
                HEADER(curr), curr, GET_SIZE(HEADER(curr)), IS_FREE(HEADER(curr)), 
                *NEXT_FREE(curr), *PREV_FREE(curr));

        curr = *NEXT_FREE(curr);
    }
    printf("[HEAP END]\n\n");
}