#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include <stddef.h>

#define SOME_MINIMUM 16
#define MAX_ALLOC_SIZE (1UL << 30) // 1 GB — reject absurdly large / wrapped-negative requests
#define MMAP_THRESHOLD (128 * 1024)
#define ALIGN16(x) (((x) + 15) & ~((size_t)15)) 

typedef enum  {FIRST_FIT, BEST_FIT} fit_strategy;

extern fit_strategy current_strategy;

typedef struct block_meta {
    size_t size;
    int free;
    int is_mmapped;
    struct block_meta *next;
    struct block_meta *previous;
} __attribute__((aligned(16))) block_meta;

typedef struct {
    size_t heap_bytes;     // total mapped from the OS
    size_t live_bytes;     // total live bytes allocated
    size_t block_count;    // nodes in the list
    size_t free_bytes;     // sum of free block sizes
    size_t largest_free;   // biggest single free block
} heap_stats;

void get_heap_stats(heap_stats *out);

extern block_meta* global_head;
extern block_meta* global_tail;
extern size_t total_bytes_allocated;
extern size_t live_bytes_allocated;

void* block_to_ptr(block_meta *block);

void set_strategy(fit_strategy s);

block_meta *find_free_block(size_t requested_size);

void split_block(size_t size, block_meta *block);

block_meta* ptr_to_block(void* user_ptr);

block_meta* make_block(size_t user_size);

void* my_malloc(size_t size);

int is_physically_adjacent(block_meta *first, block_meta *second);

void coalesce(block_meta *block);

void my_free(void* ptr);

void* my_calloc(size_t count,size_t size);

void* my_realloc(void* ptr, size_t new_size);

void print_list(void);

#endif