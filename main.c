#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#define SOME_MINIMUM 16
#define MAX_ALLOC_SIZE (1UL << 30) // 1 GB — reject absurdly large / wrapped-negative requests
#define MMAP_THRESHOLD (128 * 1024)

typedef struct block_meta {
    size_t size;
    int free;
    int is_mmapped;
    struct block_meta *next;
    struct block_meta *previous;
} __attribute__((aligned(16))) block_meta;

static block_meta* global_head = NULL;
static block_meta* global_tail = NULL;

void* block_to_ptr(block_meta *block);

block_meta *find_free_block(size_t requested_size) {
    if (requested_size == 0) {
        return NULL;
    };

    block_meta *curr = global_head;
    while (curr != NULL) {
        if (curr->free == 1 && curr->size >= requested_size) {
            return curr;
        }
        curr = curr->next;
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
        block->previous->size += block->size + sizeof(block_meta);
        block->previous->next = block->next;
        if (block->next != NULL) {
            block->next->previous = block->previous;
        };
        if (block == global_tail) {
            global_tail = block->previous;
        };
        memmove(block_to_ptr(block->previous), ptr, block->size);
        block->previous->free = 0;
        split_block(new_size,block->previous);
        return block_to_ptr(block->previous);
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

int main(int argc, char** argv) {
    (void)argc;(void)argv;

    // Phase 1: my_malloc allocates memory
    void *p1 = my_malloc(20);
    void *p2 = my_malloc(100);
    void *p3 = my_malloc(7);
    (void)p1; (void)p2; (void)p3;
    print_list();

    // Phase 2: find_free_block / my_free reuse a freed block
    void *a = my_malloc(20);
    my_free(a);
    void *b = my_malloc(10);
    printf("\na==b? %s\n", (a == b) ? "YES (reuse works)" : "NO (bug)");

    void *m1 = my_malloc(1000);
    void *m2 = my_malloc(1000);
    void *m3 = my_malloc(5000);
    void *m4 = my_malloc(1000);
    void *m5 = my_malloc(1000);
    my_free(m3);
    void *reused = my_malloc(30);
    printf("middle-block reuse: reused==m3? %s\n", (reused == m3) ? "YES" : "NO (bug)");
    (void)m1; (void)m2; (void)m4; (void)m5;

    // Phase 3: split_block carves the reused block into used + leftover
    print_list();

    // Phase 4: coalesce merges adjacent free blocks back together.
    void *q1 = my_malloc(20000);
    void *q2 = my_malloc(20000);
    void *q3 = my_malloc(20000);
    (void)q1; (void)q2; (void)q3;

    my_free(q1);
    my_free(q2);
    my_free(q3);

    print_list();

    // Phase 5: my_calloc zero-initializes memory and rejects bad input (overflow, size=0).
    // my_realloc: NULL acts like my_malloc, new_size=0 acts like my_free. Shrinking (or
    // requesting the same size) reuses split_block() to return the same block, resized
    // in place. Growing tries to absorb an adjacent free neighbor first, and only
    // falls back to a fresh my_malloc + copy + my_free if no adjacent space fits.

   char *z = (char*)my_calloc(10, sizeof(char));
    int all_zero = 1;
    for (size_t i = 0; i < 10; i++) {
        if (z[i] != 0) {
            all_zero = 0;
            break;
        };
    };
    printf("\nmy_calloc zero-initialized? %s\n", all_zero ? "YES" : "NO (bug)");

    void *overflow = my_calloc((size_t)-1, 2);
    printf("my_calloc overflow rejected? %s\n", (overflow == NULL) ? "YES" : "NO (bug)");

    void *zero_size = my_calloc(5, 0);
    printf("my_calloc size=0 rejected? %s\n", (zero_size == NULL) ? "YES" : "NO (bug)");

    void *r_null = my_realloc(NULL, 40);
    printf("\nmy_realloc(NULL, size) behaves like my_malloc? %s\n", (r_null != NULL) ? "YES" : "NO (bug)");

    void *r_freeme = my_malloc(40);
    void *r_freed = my_realloc(r_freeme, 0);
    void *r_reuse_check = my_malloc(40);
    printf("my_realloc(ptr, 0) frees block (reused after)? %s\n", (r_freed == NULL && r_reuse_check == r_freeme) ? "YES" : "NO (bug)");

    void *r_big = my_malloc(500);
    void *r_shrunk = my_realloc(r_big, 50);
    printf("my_realloc shrink keeps same pointer? %s\n", (r_shrunk == r_big) ? "YES" : "NO (bug)");

    void *r_first = my_malloc(200);
    void *r_second = my_malloc(200);
    my_free(r_second);
    void *r_grown = my_realloc(r_first, 300);
    printf("my_realloc grow merges adjacent free neighbor (same pointer)? %s\n", (r_grown == r_first) ? "YES" : "NO (bug)");

    void *r_data = my_malloc(20);
    memcpy(r_data, "hello-realloc-data", 19);
    void *r_moved = my_realloc(r_data, 50000);
    int r_data_preserved = (r_moved != NULL) && (memcmp(r_moved, "hello-realloc-data", 19) == 0);
    printf("my_realloc fallback moves and preserves data? %s\n", r_data_preserved ? "YES" : "NO (bug)");

    print_list();

    // Phase 6: requests >= MMAP_THRESHOLD get their own mmap'd block (is_mmapped=1),
    // kept out of the shared free list, and my_free() munmaps them directly.
    void *mmap_big_block = my_malloc(128*1024);
    block_meta *is_mmapped_block = ptr_to_block(mmap_big_block);
    printf("is this is_mmapped block? %s\n", (is_mmapped_block->is_mmapped) ? "YES" : "NO (bug)");

    void *below_threshold = my_malloc(128 * 1024 - 1);
    block_meta *below_block = ptr_to_block(below_threshold);
    printf("below-threshold block NOT is_mmapped? %s\n", (!below_block->is_mmapped) ? "YES" : "NO (bug)");

    void *mmap_to_free = my_malloc(200000);
    my_free(mmap_to_free);
    void *after_free_small = my_malloc(20);
    printf("mmap'd block munmapped, not reused via free list? %s\n", (after_free_small != mmap_to_free) ? "YES" : "NO (bug)");

    void *mmap_grow = my_malloc(200000);
    memset(mmap_grow, 0xAB, 200000);
    void *mmap_grown = my_realloc(mmap_grow, 400000);
    int mmap_grow_preserved = (mmap_grown != mmap_grow) && (((unsigned char*)mmap_grown)[0] == 0xAB) && (((unsigned char*)mmap_grown)[199999] == 0xAB);
    printf("my_realloc grows mmap'd block via move, data preserved? %s\n", mmap_grow_preserved ? "YES" : "NO (bug)");

    void *mmap_shrink = my_malloc(200000);
    void *mmap_shrunk = my_realloc(mmap_shrink, 150000);
    printf("my_realloc shrinks mmap'd block in place (same pointer)? %s\n", (mmap_shrunk == mmap_shrink) ? "YES" : "NO (bug)");

    print_list();

    return 0;
};