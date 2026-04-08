#include "MemoryAllocator.h"
#include <stddef.h> // size_t, offsetof
#include <unistd.h> // sbrk, brk
#include <stdio.h> // debug purposes
#include <string.h>
#include <pthread.h>

// implementation based on document below
// https://sourceware.org/glibc/wiki/MallocInternals

#define QWORD 8
#define QWORD_BIT 3
#define DQWORD 16
#define DQWORD_BIT 4
#define MIN_CHUNK_SIZE 32
#define RELEASE_THRESHOLD (128 * 1024) // 128KB
// padding for 64-bit systems
#define ALIGN(x) (((((x) - 1) >> DQWORD_BIT) << DQWORD_BIT) + DQWORD)
#define SIZE(mchunkptr) (((mchunkptr)->hdr) & ~1)
#define PREV_INUSE(mchunkptr) (((mchunkptr)->hdr) & 1)
#define BINS_COUNT 10 // 64, 128, 256, 512, 1K, 2K, 4K, 8K, 16K, Large
#define PAGE_SIZE 4096

static mchunk dummyheads[BINS_COUNT];
static mchunk *seg_free_list[BINS_COUNT];

static int get_bin_index(size_t size)
{
	size /= 64;
	int index = 0;
	
	while (size && index < BINS_COUNT - 1) {
		size >>= 2;
		index++;
	}

	return index;
}

static mchunk *get_rchunk(mchunk *mchunkptr)
{
	return (mchunk *)((char *)mchunkptr + SIZE(mchunkptr));
}

static mchunk *get_lchunk(mchunk *mchunkptr)
{
	return (mchunk *)((char *)mchunkptr - mchunkptr->prev_size);
}

static void remove_node(mchunk *node)
{
	mchunk* rchunk = get_rchunk(node);
	rchunk->hdr |= 1; // prev in use = 1

	mchunk* fwd = node->fwd;
	mchunk* bck = node->bck;
	bck->fwd = fwd;

	if(fwd)
		fwd->bck = bck;
}

static void absorb_node(mchunk *node)
{
	// just absrob a free node to another free node, nothing "prev in use" changes
	mchunk* fwd = node->fwd;
	mchunk* bck = node->bck;

	bck->fwd = fwd;
	
	if(fwd)
		fwd->bck = bck;
}

static void insert_node(mchunk *node)
{
	mchunk* rchunk = get_rchunk(node);
	rchunk->hdr &= ~1; // prev in use = 0

	mchunk *dummyhead = seg_free_list[get_bin_index(SIZE(node))];
	node->fwd = dummyhead->fwd;
	node->bck = dummyhead;

	if(node->fwd)
		node->fwd->bck = node;
	
	dummyhead->fwd = node;
}

static void *extend_heap(size_t size)
{
    	size_t sbrk_size = PAGE_SIZE > size ? PAGE_SIZE : size;
	void *new_chunk = sbrk(sbrk_size);

    	if (new_chunk == (void *)-1) { // no more heap left
        	fprintf(stderr, "ERROR: HEAP EXHAUSTION\n");
        	return NULL;
    	}

	// consider epilogue header (16 bytes)
	// don't have to change prev size and prev in use
    	mchunk *mchunkptr = (mchunk *)((char *)new_chunk - DQWORD);
    	mchunkptr->hdr = sbrk_size | PREV_INUSE(mchunkptr);
    	insert_node(mchunkptr);

	mchunk *epilogue = get_rchunk(mchunkptr);
	epilogue->prev_size = sbrk_size;
	epilogue->hdr = 0 | 0;

    	return mchunkptr;
}

static void *find_chunk(size_t size)
{
	int index = get_bin_index(size) + 1;

	for (; index < BINS_COUNT; index++) {
		mchunk *dummyhead = seg_free_list[index];
		mchunk *curr = dummyhead->fwd;

		while (curr) {
			if(SIZE(curr) && SIZE(curr) >= size)
				return curr;
			curr = curr->fwd;
		}
	}

	return NULL;
}

static void split_chunk(mchunk *mchunkptr, size_t size)
{
	if(mchunkptr->hdr < size + MIN_CHUNK_SIZE)
		return;

	mchunk *new_mchunkptr = (mchunk *)((char *)mchunkptr + size);
	// total size - split size, prev in use = 0
	// prev in use cancels out whether 0 or 1
	new_mchunkptr->hdr = mchunkptr->hdr - size;
	new_mchunkptr->prev_size = size;
	insert_node(new_mchunkptr);

	// [original chunk] [new chunk] | [next chunk]
	mchunk *next_mchunkptr = get_rchunk(new_mchunkptr);
	// prev in use = 0
	next_mchunkptr->prev_size = SIZE(new_mchunkptr);

	mchunkptr->hdr = size | PREV_INUSE(mchunkptr);
}

static mchunk* fuse_chunk(mchunk *mchunkptr)
{
	// not start of heap & prev in use = 0
	if (mchunkptr->prev_size > 0 && !PREV_INUSE(mchunkptr)) {
		mchunk *lchunk = get_lchunk(mchunkptr);
		mchunk *rchunk = get_rchunk(mchunkptr);
		lchunk->hdr += SIZE(mchunkptr);
		rchunk->prev_size = SIZE(lchunk);
		// mchunkptr is assumed to be free
		// we should not manipulate active memory
		absorb_node(mchunkptr);
		remove_node(lchunk);
		insert_node(lchunk);
		mchunkptr = lchunk;
	}

	mchunk* rchunk = (mchunk *)((char *)mchunkptr + SIZE(mchunkptr));
	
	 // epilogue hdr
	if (SIZE(rchunk) == 0)
		return mchunkptr;

	mchunk* rrchunk = (mchunk *)((char *)rchunk + SIZE(rchunk)); // well prev_in_use in rchunk is already 0
	
	// rchunk in use
	if (PREV_INUSE(rrchunk))
		return mchunkptr;

	// rchunk->prev (mchunkptr) in use = 0
	mchunkptr->hdr += rchunk->hdr;
	rrchunk->prev_size = SIZE(mchunkptr);
	absorb_node(rchunk);
	remove_node(mchunkptr);
	insert_node(mchunkptr);

	return mchunkptr;
}

static void my_memcpy(void *dest, mchunk *src)
{
	size_t size = SIZE(src) - QWORD;
	size_t i;

	for (i = 0; i + QWORD <= size; i += QWORD) {
		size_t *dest_register = (size_t *)((char *)dest + i);
		size_t *src_register = (size_t *)((char *)(src->payload) + i);
		*dest_register = *src_register;
	}
	
	for (; i < size; i++) {
		char *dest_byte = (char *)dest + i;
		char *src_byte = (char *)(src->payload) + i;
		*dest_byte = *src_byte;
	}
}

static int malloc_init = 0;

void static my_malloc_init()
{
    	mchunk *epilogue = (mchunk *)sbrk(DQWORD);
    
	if (epilogue == (mchunk *)-1)
        	return;
	
	// prev size = 0;
	// size = 16, prev in use = 1
	epilogue->prev_size = 0;
	epilogue->hdr = DQWORD | 1;

    	for (int i=0; i < BINS_COUNT; i++) {
        	dummyheads[i].hdr = 0;
        	dummyheads[i].fwd = NULL;
        	dummyheads[i].bck = NULL;
        	seg_free_list[i] = &dummyheads[i];
    	}

    	malloc_init = 1;
}

static pthread_mutex_t malloc_lock = PTHREAD_MUTEX_INITIALIZER;

static void *my_malloc_unlocked(size_t size);

void *my_malloc(size_t size)
{
    	pthread_mutex_lock(&malloc_lock);
    	void *ptr = my_malloc_unlocked(size);
    	pthread_mutex_unlock(&malloc_lock);
    	return ptr;
}

static void *my_malloc_unlocked(size_t size)
{
    	if(!malloc_init)
        	my_malloc_init();

    	size_t aligned_size = ALIGN(size + DQWORD);
    	size_t chunk_size = aligned_size > MIN_CHUNK_SIZE ? aligned_size : MIN_CHUNK_SIZE;

    	// base = NULL > return NULL
    	// no suitable free chunk > return NULL
    	// suitable free chunk > return mchunk*
    	mchunk *mchunkptr = find_chunk(chunk_size);

    	if (mchunkptr == NULL) {
        	mchunkptr = extend_heap(chunk_size);
        
		if (mchunkptr == NULL)
            		return NULL;

        	mchunkptr = fuse_chunk(mchunkptr);
        	split_chunk(mchunkptr, chunk_size);
		remove_node(mchunkptr);

        	return (void *)mchunkptr->payload;
    	}

    	// found suitable free chunk
    	split_chunk(mchunkptr, chunk_size);
    	remove_node(mchunkptr);

    	return (void *)mchunkptr->payload;
}

static void my_free_unlocked(void* ptr);

void my_free(void* ptr)
{
	pthread_mutex_lock(&malloc_lock);
	my_free_unlocked(ptr);
	pthread_mutex_unlock(&malloc_lock);
}

static void my_free_unlocked(void* ptr){
	if (ptr == NULL)
		return;

	mchunk *mchunkptr = (mchunk *)((char *)ptr - DQWORD);
	insert_node(mchunkptr);
	mchunkptr = fuse_chunk(mchunkptr);

	mchunk *rchunk = get_rchunk(mchunkptr);

	// epilogue header + returning memory to system
	if (SIZE(rchunk) == 0 && SIZE(mchunkptr) > RELEASE_THRESHOLD) {
		remove_node(mchunkptr);
		mchunkptr->hdr = PREV_INUSE(mchunkptr); // epilogue hdr : 0 | prev in use
		int retval = brk(mchunkptr->payload);

		if (retval < 0)
			fprintf(stderr, "brk error");
	}
}

void *my_calloc(size_t number, size_t size)
{
	size_t *new_mem = my_malloc(number * size);
	size_t clear_len = number * size / QWORD;

	for (int i = 0; i < clear_len; i++)
		new_mem[i] = 0;
	
	return (void *)new_mem;
}

static void *my_realloc_unlocked(void *ptr, size_t size);

void *my_realloc(void *ptr, size_t size)
{
	pthread_mutex_lock(&malloc_lock);
	void *new_ptr = my_realloc_unlocked(ptr, size);
	pthread_mutex_unlock(&malloc_lock);
	
	return new_ptr;
}

static void *my_realloc_unlocked(void *ptr, size_t size)
{
	if(ptr == NULL)
		return my_malloc_unlocked(size);

	size_t aligned_size = ALIGN(size + DQWORD);
	size_t chunk_size = aligned_size > MIN_CHUNK_SIZE ? aligned_size : MIN_CHUNK_SIZE;
	mchunk *mchunkptr = (mchunk *)((char *)ptr - DQWORD);

	if (chunk_size <= SIZE(mchunkptr) + MIN_CHUNK_SIZE) {
		split_chunk(mchunkptr, chunk_size);
		mchunk* rchunk = (mchunk *)((char *)mchunkptr + SIZE(mchunkptr));
		fuse_chunk(rchunk);

		return (void *)mchunkptr->payload;
	}

	// chunk_size > SIZE(mchunkptr)
	mchunk *rchunk = (mchunk *)((char *)mchunkptr + SIZE(mchunkptr));
	
	// epilogue hdr
	if (SIZE(rchunk) == 0) {
		extend_heap(chunk_size - SIZE(mchunkptr));
		remove_node(rchunk);
		mchunkptr->hdr += SIZE(rchunk);
		
		return (void *)mchunkptr->payload;
	}

	mchunk *rrchunk = (mchunk *)((char *)rchunk + SIZE(rchunk));

	// rchunk is not sufficient for expansion
	if (PREV_INUSE(rrchunk) || SIZE(mchunkptr) + SIZE(rchunk) < chunk_size) {
		void *new_payload = my_malloc_unlocked(size);
		my_memcpy(new_payload, mchunkptr);
		my_free_unlocked(mchunkptr->payload);
		
		return (void *)new_payload;
	}

	// right chunk is sufficient for expansion
	remove_node(rchunk);
	mchunkptr->hdr += SIZE(rchunk);
	split_chunk(mchunkptr, chunk_size);

	return (void *)mchunkptr->payload;
}

/*
 *  Functions for char*, string copy
 */

static void my_strcpy(char* dest, char* src)
{
	while (*src != '\0') {
		*dest = *src;
		dest++;
		src++;
	}

	*dest = '\0';
}

char *my_strdup(char *s)
{
	if(s == NULL)
		return NULL;
	
	// plus one for '\0'
	size_t len = strlen(s) + 1;
	char *new_str = my_malloc(len);

	if(new_str)
		my_strcpy(new_str, s);

	return new_str;
}



/*
 *   for debugging
 */

void debug_heap()
{
	printf("heap top: %p\n", sbrk(0));
	printf("[HEAP START]\n");

	for (int i=0; i<BINS_COUNT; i++) {
		if (!seg_free_list[i])
			continue;
		
		mchunk *curr = seg_free_list[i];
		curr = curr->fwd;

		if (curr) 
			printf("segment %d\n", i);
		
		while (curr) {
			printf("mchunkptr [%p] | prev size: %ld | size: %ld | prev in use: %ld | fwd: %p | bck: %p\n",
				curr,
				curr->prev_size,
				SIZE(curr),
				PREV_INUSE(curr),
				curr->fwd,
				curr->bck);
			curr = curr->fwd;
		}
	}

	printf("[HEAP END]\n\n");
}
