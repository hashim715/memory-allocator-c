#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
typedef struct block_meta {
    size_t size;
    int free;
    struct block_meta *next;
} __attribute__((aligned(16))) block_meta;

static block_meta* global_head = NULL;
static block_meta* global_tail = NULL;

block_meta *find_free_block(size_t requested_size) {
    block_meta *curr = global_head;
    while (curr != NULL) {
        if (curr->free == 1 && curr->size >= requested_size) {
            return curr;
        }
        curr = curr->next;
    }
    return NULL;
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
    size_t size_to_request = sizeof(block_meta) + user_size;

    void* chunk = mmap(NULL, size_to_request, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (chunk == MAP_FAILED) {
        perror("mmap failed");
        return NULL;
    };

    block_meta* header = (block_meta*)chunk;

    header->size = user_size;
    header->free = 0;
    header->next = NULL;

    return header;
};

void* my_malloc(size_t size) {
    if (size == 0) {
        return NULL;
    };

    block_meta* new_block = find_free_block(size);

    if (new_block != NULL) {
        new_block->free = 0;
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