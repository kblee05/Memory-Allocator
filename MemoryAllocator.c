#include "MemoryAllocator.h"
#include <stddef.h> // size_t, offsetof
#include <unistd.h> // sbrk, brk
#include <stdio.h> // debug purposes

// implementation based on document below
// https://sourceware.org/glibc/wiki/MallocInternals

#define QWORD 8
#define QWORD_BIT 3
#define MIN_CHUNK_SIZE 32
#define RELEASE_THRESHOLD (128 * 1024) // 128KB
// padding for 64-bit systems
#define ALIGN(x) (((((x) - 1) >> QWORD_BIT) << QWORD_BIT) + QWORD) // -O0 compilers maybe?
#define SIZE(mchunkptr) (((mchunkptr)->hdr) & ~1)
#define PREV_INUSE(mchunkptr) (((mchunkptr)->hdr) & 1)
#define BINS_COUNT 10 // 32, 64, 128, 256, 512, 1K, 2K, 4K, 8K, Large

static mchunk dummyheads[BINS_COUNT];
static mchunk *seg_free_list[BINS_COUNT];

static size_t get_bin_index(size_t size){
    if(size < 64) return 0;
    if(size < 128) return 1;
    if(size < 256) return 2;
    if(size < 512) return 3;
    if(size < 1024) return 4;
    if(size < 2048) return 5;
    if(size < 4096) return 6;
    if(size < 8192) return 7;
    if(size < 16384) return 8;
    return 9;
}

static void remove_node(mchunk *node){
    mchunk* rchunk = (mchunk*)((char *)node + SIZE(node));
    rchunk->hdr = rchunk->hdr | 1; // prev in use = 1

    mchunk* fwd = node->fwd;
    mchunk* bck = node->bck;

    bck->fwd = fwd;
    
    if(fwd)
        fwd->bck = bck;
}

static void absorb_node(mchunk *node){
    // just absrob a free node to another free node, nothing "prev in use" changes
    mchunk* fwd = node->fwd;
    mchunk* bck = node->bck;

    bck->fwd = fwd;
    
    if(fwd)
        fwd->bck = bck;
}

static void insert_node(mchunk *node){
    mchunk* rchunk = (mchunk*)((char *)node + SIZE(node));
    rchunk->hdr = rchunk->hdr & ~1; // prev in use = 0

    mchunk *dummyhead = seg_free_list[get_bin_index(SIZE(node))];
    node->fwd = dummyhead->fwd;
    node->bck = dummyhead;

    if(node->fwd)
        node->fwd->bck = node;
    
    dummyhead->fwd = node;
}

static void *extend_heap(size_t size){
    size_t *new_chunk = (size_t *) sbrk(size);
    if(new_chunk == (size_t *)-1){ // no more heap left
        return NULL;
    }

    mchunk *mchunkptr = (mchunk *)((char *)new_chunk - 2 * QWORD);
    mchunkptr->hdr = size | PREV_INUSE(mchunkptr); // success prev in use
    size_t *prev_size = (size_t *)((char *)mchunkptr + size);
    *prev_size = size;
    insert_node(mchunkptr);

    mchunk *epilogue_hdr = (mchunk *)((char *)mchunkptr + size + QWORD);
    epilogue_hdr->hdr = 0 | 0; // size = 0, prev in-use = 0
    return mchunkptr;
}

static void *find_chunk(size_t size){
    size_t index = get_bin_index(size) + 1;

    for(; index < BINS_COUNT; index++){
        mchunk *dummyhead = seg_free_list[index];
        mchunk *curr = dummyhead->fwd;
        while(curr){
            if(SIZE(curr) && SIZE(curr) >= size){ // Free & Enough size
                return curr;
            }
            curr = curr->fwd;
        }
    }
    return NULL;
}

static void split_chunk(mchunk *mchunkptr, size_t size){
    if(mchunkptr->hdr < size + MIN_CHUNK_SIZE)
        return;

    mchunk *new_mchunkptr = (mchunk *)((char *)mchunkptr + size);
    new_mchunkptr->hdr = mchunkptr->hdr - size; // total size - split size, prev in use = 0
    new_mchunkptr->prev_size = size;

    // [original chunk] [new chunk] | [next chunk]
    mchunk *next_mchunkptr = (mchunk *)((char *)new_mchunkptr + SIZE(new_mchunkptr));
    next_mchunkptr->prev_size = new_mchunkptr->hdr;

    // original chunk
    mchunkptr->hdr = size | PREV_INUSE(mchunkptr);

    insert_node(new_mchunkptr);
}

static mchunk* fuse_chunk(mchunk *mchunkptr){
    if((mchunkptr->prev_size & ~1) && !PREV_INUSE(mchunkptr)){ // not prologue header & prev in use = 0
        mchunk *lchunk = (mchunk *)((char *)mchunkptr - mchunkptr->prev_size);
        lchunk->hdr += SIZE(mchunkptr);
        mchunk *rchunk = (mchunk *)((char *)lchunk + SIZE(lchunk));
        rchunk->prev_size = SIZE(lchunk);
        absorb_node(mchunkptr);
        remove_node(lchunk);
        insert_node(lchunk);
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

    mchunkptr->hdr += rchunk->hdr; // rchunk prev in use = 0
    size_t *prev_size = (size_t *)((char *)mchunkptr + SIZE(mchunkptr));
    *prev_size = SIZE(mchunkptr);
    absorb_node(rchunk);
    remove_node(mchunkptr);
    insert_node(mchunkptr);
    return mchunkptr;
}

static void my_memcpy(void *dest, mchunk *src){
    size_t size = SIZE(src) - QWORD;
    size_t i;

    for(i = 0; i + QWORD <= size; i += QWORD){
        size_t *dest_register = (size_t *)((char *)dest + i);
        size_t *src_register = (size_t *)((char *)(src->payload) + i);
        *dest_register = *src_register;
    }
    
    for(; i < size; i++){
        char *dest_byte = (char *)dest + i;
        char *src_byte = (char *)(src->payload) + i;
        *dest_byte = *src_byte;
    }
}

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

    for(size_t i=0; i<BINS_COUNT; i++){
        dummyheads[i].hdr = 0;
        dummyheads[i].fwd = NULL;
        dummyheads[i].bck = NULL;
        seg_free_list[i] = &dummyheads[i];
    }

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
        return (void *)mchunkptr->payload;
    }

    // found suitable free chunk
    remove_node(mchunkptr);
    split_chunk(mchunkptr, chunk_size);
    return (void *)mchunkptr->payload;
}

void my_free(void* ptr){
    mchunk *mchunkptr = (mchunk *)((char *)ptr - 2 * QWORD);
    insert_node(mchunkptr);
    mchunkptr = fuse_chunk(mchunkptr);
    mchunk *rchunk = (mchunk *)((char *)mchunkptr + SIZE(mchunkptr));
    rchunk->prev_size = SIZE(mchunkptr);
    
    if(!SIZE(rchunk) && SIZE(mchunkptr) > RELEASE_THRESHOLD){ // rchunk = epilogue hdr, size > 128KB
        remove_node(mchunkptr);
        mchunkptr->hdr = PREV_INUSE(mchunkptr); // epilogue hdr : 0 | prev in use
        brk(mchunkptr->payload);
    }
}

void *my_calloc(size_t number, size_t size){
    size_t *new = my_malloc(number * size);
    mchunk *mchunkptr = (mchunk *)((char *)new - 2 * QWORD);
    size_t clear_len = SIZE(mchunkptr) - QWORD; // padded by 8bytes
    for(size_t i = 0; i * QWORD < clear_len; i++){
        new[i] = 0;
    }
    return (void *)new;
}

void *my_realloc(void *ptr, size_t size){
    size_t aligned_size = ALIGN(size + QWORD);
    size_t chunk_size = aligned_size > MIN_CHUNK_SIZE ? aligned_size : MIN_CHUNK_SIZE;
    mchunk *mchunkptr = (mchunk *)((char *)ptr - 2 * QWORD);

    if(chunk_size <= SIZE(mchunkptr) + MIN_CHUNK_SIZE){ // split
        split_chunk(mchunkptr, chunk_size);
        mchunk* rchunk = (mchunk *)((char *)mchunkptr + SIZE(mchunkptr));
        fuse_chunk(rchunk);
        return (void *)mchunkptr->payload;
    }

    if(chunk_size > SIZE(mchunkptr)){
        mchunk *rchunk = (mchunk *)((char *)mchunkptr + SIZE(mchunkptr));
        
        if(!SIZE(rchunk)){ // epilogue hdr
            extend_heap(chunk_size - SIZE(mchunkptr)); // rchunk -> new chunk
            remove_node(rchunk);
            mchunkptr->hdr += SIZE(rchunk);
            return (void *)mchunkptr->payload;
        }

        mchunk *rrchunk = (mchunk *)((char *)rchunk + SIZE(rchunk));
        if(PREV_INUSE(rrchunk) || SIZE(mchunkptr) + SIZE(rchunk) < chunk_size){ // rchunk is not sufficient for expansion
            void *new_payload = my_malloc(size);
            my_memcpy(new_payload, mchunkptr); // dest, source
            my_free(mchunkptr->payload);
            return (void *)new_payload;
        }

        // right chunk is sufficient for expansion
        remove_node(rchunk);
        mchunkptr->hdr += SIZE(rchunk);
        split_chunk(mchunkptr, chunk_size);
        return (void *)mchunkptr->payload;
    }
}



// for debugging

void debug_heap(){
    printf("heap top: %p\n", sbrk(0));
    printf("[HEAP START]\n");

    for(int i=0; i<BINS_COUNT; i++){
        if(!seg_free_list[i])
            continue;
        
        mchunk *curr = seg_free_list[i];
        curr = curr->fwd;
        if(curr) printf("segment %d\n", i);
        while(curr){
            printf("mchunkptr [%p] | size: %ld | prev in use: %ld | fwd: %p | bck: %p\n", curr, SIZE(curr), PREV_INUSE(curr), curr->fwd, curr->bck);
            curr = curr->fwd;
        }
    }

    printf("[HEAP END]\n\n");
}