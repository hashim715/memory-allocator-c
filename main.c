#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>

#define SOME_MINIMUM 16
#define MAX_ALLOC_SIZE (1UL << 30) // 1 GB — reject absurdly large / wrapped-negative requests

typedef struct block_meta {
    size_t size;
    int free;
    struct block_meta *next;
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
    }
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
        block->next = new_block;
        if (block == global_tail) {
            global_tail = new_block;
        };
    };

    block->size = size; 
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

    return header;
};

void* my_malloc(size_t size) {
    if (size == 0 || size > MAX_ALLOC_SIZE) {
        return NULL;
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
        global_tail = new_block;
    };

    return block_to_ptr(new_block);
};

void my_free(void* ptr) {
    block_meta* block = ptr_to_block(ptr);

    if (block == NULL) {
        return;
    };

    block->free = 1;
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
    
    void *p1 = my_malloc(20);
    void *p2 = my_malloc(100);
    void *p3 = my_malloc(7);
    (void)p1; (void)p2; (void)p3;

    print_list();

    void *a = my_malloc(20);
    my_free(a);
    void *b = my_malloc(10);
    printf("\na==b? %s\n", (a == b) ? "YES (reuse works)" : "NO (bug)");

    void *m1 = my_malloc(1000);
    void *m2 = my_malloc(1000);
    void *m3 = my_malloc(50);
    void *m4 = my_malloc(1000);
    void *m5 = my_malloc(1000);
    my_free(m3);
    void *reused = my_malloc(30);

    printf("middle-block reuse: reused==m3? %s\n", (reused == m3) ? "YES" : "NO (bug)");
    (void)m1; (void)m2; (void)m4; (void)m5;

    print_list();

    return 0;
};