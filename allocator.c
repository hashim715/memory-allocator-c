#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include "allocator.h"

fit_strategy current_strategy = FIRST_FIT;
block_meta* global_head = NULL;
block_meta* global_tail = NULL;
size_t total_bytes_allocated = 0;
size_t live_bytes_allocated = 0;

void set_strategy(fit_strategy s) {
    current_strategy = s;
};

void get_heap_stats(heap_stats *out) {
    block_meta* curr = global_head;
    out->block_count = 0;
    out->free_bytes = 0;
    out->largest_free = 0;
    out->heap_bytes = total_bytes_allocated;
    out->live_bytes = live_bytes_allocated;

    size_t total_bytes = 0;

    while (curr != NULL) {
        out->block_count += 1;
        total_bytes += curr->size + sizeof(block_meta);

        if (curr->free == 1) {
            out->free_bytes += curr->size;

            if (curr->size > out->largest_free) {
                out->largest_free = curr->size;
            };
        };

        curr = curr->next;
    };

    if (total_bytes != out->heap_bytes) {
        fprintf(stderr, "Warning: heap accounting mismatch (list=%zu, counter=%zu)\n",
                total_bytes, out->heap_bytes);
    };
};

// header -> user pointer (move FORWARD past the header)
void* block_to_ptr(block_meta *block) {
    if (block == NULL) {
        return NULL;
    };

    return (void*)(block + 1);
};

// user pointer -> header (move BACKWARD to find the header)
block_meta* ptr_to_block(void* user_ptr) {
    if (user_ptr == NULL) {
        return NULL;
    };

    return (block_meta*)user_ptr - 1;
};

block_meta *find_free_block(size_t requested_size) {
    if (requested_size == 0) {
        return NULL;
    };

    if (current_strategy == FIRST_FIT) {
        block_meta *curr = global_head;
        while (curr != NULL) {
            if (curr->free == 1 && curr->size >= requested_size) {
                return curr;
            }
            curr = curr->next;
        };
    } else {
        block_meta *curr = global_head;
        block_meta *best_fit = NULL;
        while (curr != NULL) {
            if (curr->free == 1 && curr->size >= requested_size) {
                if (curr->size == requested_size) return curr;   // perfect fit
                if (best_fit == NULL || curr->size < best_fit->size) {
                    best_fit = curr;
                };
            };
            curr = curr->next;
        };
        return best_fit;
    };

    return NULL;
};

void split_block(size_t size, block_meta *block) {
    if (size == 0) {
        return;
    };

    size_t requested_size = size + sizeof(block_meta);

    block_meta *new_block = NULL;

    if (block->size >= requested_size && (block->size - requested_size) >= (sizeof(block_meta) + (size_t)SOME_MINIMUM)) {
        size_t leftover = block->size - requested_size;
        // split
        new_block = (block_meta*)((char*)block_to_ptr(block) + size);
        new_block->size = (size_t)leftover;
        new_block->free = 1;
        new_block->is_mmapped = 0;
        new_block->next = NULL;
    };

    if (new_block != NULL) {
        new_block->next = block->next;
        new_block->previous = block;
        if (block->next != NULL) {
            block->next->previous = new_block;
        }
        block->next = new_block;
        if (block == global_tail) {
            global_tail = new_block;
        };
        block->size = size; 
    };

    block->free = 0;
};

// a real, reusable helper: mmap a chunk and set up its header properly
block_meta* make_block(size_t user_size) {
    if (user_size == 0) {
        return NULL;
    };

    size_t page_size = sysconf(_SC_PAGESIZE);

    size_t raw_needed = user_size + sizeof(block_meta);

    size_t rounded = ((raw_needed + page_size - 1) / page_size) * page_size; // round up to next page

    void* chunk = mmap(NULL, rounded, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (chunk == MAP_FAILED) {
        perror("mmap failed");
        return NULL;
    };

    block_meta* header = (block_meta*)chunk;

    header->size = rounded - sizeof(block_meta); // <-- store the TRUE usable size, not user_size
    header->free = 0;
    header->next = NULL;
    header->previous = NULL;
    header->is_mmapped = 0;

    return header;
};

void* my_malloc(size_t size) {
    if (size == 0 || size > MAX_ALLOC_SIZE) {
        return NULL;
    };

    if (size >= MMAP_THRESHOLD) {
        block_meta* new_block = make_block(size);
        if (new_block == NULL) return NULL;
        new_block->is_mmapped = 1;
        return block_to_ptr(new_block);
    };

    size = ALIGN16(size);

    block_meta* new_block = find_free_block(size);

    if (new_block != NULL) {
        split_block(size, new_block);
        return block_to_ptr(new_block);
    };

    new_block = make_block(size);

    if (new_block == NULL) return NULL;

    if (global_head == NULL) {
        // Case A: list was empty. New node becomes BOTH head and tai
        global_head = new_block;
        global_tail = new_block;
    } else {
        // Case B: list already has at least one node (works for 1, 2, or 1000).
        global_tail->next = new_block;
        new_block->previous = global_tail;
        global_tail = new_block;
    };

    total_bytes_allocated += new_block->size + sizeof(block_meta);

    split_block(size, new_block);
    return block_to_ptr(new_block);
};

int is_physically_adjacent(block_meta *first, block_meta *second) {
    char *end_of_first = (char*)block_to_ptr(first) + first->size;
    return (char*)second == end_of_first;
};

void coalesce(block_meta *block) {
    if (block->next != NULL && block->next->free == 1 && is_physically_adjacent(block, block->next)) {
        block->size += block->next->size + sizeof(block_meta);
        if (block->next->next != NULL) {
            block->next->next->previous = block;
        };
        if (block->next == global_tail) {
            global_tail = block;
        };
        block->next = block->next->next;
    };

    if (block->previous != NULL && block->previous->free == 1 && is_physically_adjacent(block->previous,block)) {
        block->previous->size += block->size + sizeof(block_meta);
        block->previous->next = block->next;
        if (block->next != NULL) {
            block->next->previous = block->previous;
        };
        if (block == global_tail) {
            global_tail = block->previous;
        };
    };
};

void my_free(void* ptr) {
    block_meta* block = ptr_to_block(ptr);

    if (block == NULL) {
        return;
    };

    if (block->is_mmapped) {
        int result = munmap(block,block->size + sizeof(block_meta));
        
        if (result == -1) {
            perror("munmap failed");
        };
        return;
    };

    block->free = 1;
    coalesce(block);
};

void* my_calloc(size_t count,size_t size) {
    if (size == 0 || size > MAX_ALLOC_SIZE) {
        return NULL;
    };

    size_t total = size * count;

    if (count != 0 && total / count != size) return NULL;

    void* ptr = my_malloc(total);

    if (ptr == NULL) return NULL;   // <-- add this

    memset(ptr, 0, total);

    return ptr;
};

void* my_realloc(void* ptr, size_t new_size) {
    if (new_size > MAX_ALLOC_SIZE) {
        return NULL;
    };

    if (ptr == NULL) return my_malloc(new_size);

    if (new_size == 0) {
        my_free(ptr);
        return NULL;
    };

    block_meta* block = ptr_to_block(ptr);

    if (block->is_mmapped) {
        if (new_size > block->size) {
            void* new_ptr = my_malloc(new_size);
            if (new_ptr == NULL) return NULL;
            memcpy(new_ptr,ptr,block->size);
            my_free(ptr);
            return new_ptr;
        } else {
            return ptr;
        };
    };

    new_size = ALIGN16(new_size);

    if (new_size <= block->size) {
        split_block(new_size, block);
        return block_to_ptr(block);
    };

    if (block->next != NULL && block->next->free == 1 && is_physically_adjacent(block, block->next) && block->size + block->next->size + sizeof(block_meta) >= new_size) {
        block->size += block->next->size + sizeof(block_meta);
        if (block->next->next != NULL) {
            block->next->next->previous = block;
        };
        if (block->next == global_tail) {
            global_tail = block;
        };
        block->next = block->next->next;
        split_block(new_size,block);
        return block_to_ptr(block);
    };

    if (block->previous != NULL && block->previous->free == 1 && is_physically_adjacent(block->previous,block) && block->previous->size + block->size + sizeof(block_meta) >= new_size) {
        block_meta *prev = block->previous;
        size_t old_size = block->size;
        prev->size += block->size + sizeof(block_meta);
        prev->next = block->next;
        if (block->next) block->next->previous = prev;
        if (block == global_tail) global_tail = prev;
        // memmove, not memcpy: source and destination can overlap here (the previous
        // block's header sits inside/adjacent to the region we're copying from).
        memmove(block_to_ptr(prev), ptr, old_size);
        prev->free = 0;
        split_block(new_size, prev);
        return block_to_ptr(prev);
    };

    void* new_ptr = my_malloc(new_size);

    if (new_ptr == NULL) return NULL;

    memcpy(new_ptr,ptr,block->size);

    my_free(ptr);

    return new_ptr;
};

void print_list(void) {
    block_meta *b = global_head;
    int i = 0;
    printf("--- list ---\n");
    while (b != NULL) {
        printf("  [%d] @ %p size=%zu free=%d next=%p\n", i, (void*)b, b->size, b->free, (void*)b->next);
        b = b->next;
        i++;
    }
    printf("head=%p tail=%p\n", (void*)global_head, (void*)global_tail);
};